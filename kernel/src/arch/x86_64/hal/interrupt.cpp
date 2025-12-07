#include "hal/interrupt.hpp"
#include "cpu/exception.hpp"
#include "hal/cpu.hpp"
#include "hal/interface/interrupt.hpp"
#include "libs/log.hpp"
#include "hal/lapic.hpp"

namespace kernel::cpu::arch {
IInterruptHandler* InterruptDispatcher::handlers[256] = {nullptr};

void InterruptDispatcher::register_handler(uint8_t vector, IInterruptHandler* handler) {
    handlers[vector] = handler;
    LOG_INFO("IDT: registered handler '%s' for vector %u", handler ? handler->name() : "<null>",
             vector);
}

void InterruptDispatcher::unregister_handler(uint8_t vector) {
    LOG_INFO("IDT: unregistered handler '%s' for vector %u",
             handlers[vector] ? handlers[vector]->name() : "<null>", vector);
    handlers[vector] = nullptr;
}

void InterruptDispatcher::dispatch(TrapFrame* frame) {
    PerCPUData* cpu = CPUCoreManager::get_curr_cpu();

    uint8_t vector = static_cast<uint8_t>(frame->vector);

    if (handlers[vector]) {
        IrqStatus status = handlers[vector]->handle(frame);

        if (status == IrqStatus::Reschedule) {
            // If a driver is unblocked by this IRQ, we invoke the scheduler
            // to switch to it immediately.
            LOG_DEBUG("IDT: vector %u requested reschedule on CPU %u", vector, cpu->cpu_id);
        }
    } else {
        default_handler(frame, cpu->cpu_id);
    }

    if (vector >= 32) {
        hal::Lapic::send_eoi();
    }
}

void InterruptDispatcher::default_handler(TrapFrame* frame, uint32_t cpu_id) {
    if (frame->vector < 32) {
        PANIC("[CPU %d] FATAL EXCEPTION: Vector %lu Error %lu", cpu_id, frame->vector,
              frame->error_code);
    } else {
        LOG_WARN("[CPU %d] Unhandled interrupt: Vector %lu", cpu_id, frame->vector);
    }
}
}  // namespace kernel::cpu::arch