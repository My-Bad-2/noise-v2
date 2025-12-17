#include "hal/interrupt.hpp"
#include "cpu/exception.hpp"
#include "hal/cpu.hpp"
#include "hal/interface/interrupt.hpp"
#include "libs/log.hpp"
#include "hal/lapic.hpp"
#include "hal/ioapic.hpp"
#include "cpu/registers.hpp"
#include "task/scheduler.hpp"

namespace kernel::cpu::arch {
namespace {
uint64_t eoi_bitmap[4];

bool get_eoi(uint8_t vector) {
    const int byte = vector / (sizeof(uint64_t) * 8);
    const int bit  = vector % (sizeof(uint64_t) * 8);

    return eoi_bitmap[byte] & (1 << bit);
}

void set_eoi(uint8_t vector) {
    const int byte = vector / (sizeof(uint64_t) * 8);
    const int bit  = vector % (sizeof(uint64_t) * 8);

    eoi_bitmap[byte] |= (1 << bit);
}

void clear_eoi(uint8_t vector) {
    const int byte = vector / (sizeof(uint64_t) * 8);
    const int bit  = vector % (sizeof(uint64_t) * 8);

    eoi_bitmap[byte] &= ~(1u << bit);
}

void send_eoi(uint8_t vector) {
    if (hal::Lapic::is_ready()) {
        hal::Lapic::send_eoi();
    } else {
        hal::IOAPIC::send_eoi(vector);
    }
}
}  // namespace

IInterruptHandler* InterruptDispatcher::handlers[256] = {nullptr};

void InterruptDispatcher::register_handler(uint8_t vector, IInterruptHandler* handler,
                                           bool eoi_first) {
    handlers[vector] = handler;

    if (eoi_first) {
        set_eoi(vector);
    }

    LOG_INFO("IDT: registered handler '%s' for vector %u", handler ? handler->name() : "<null>",
             vector);
}

void InterruptDispatcher::unregister_handler(uint8_t vector) {
    LOG_INFO("IDT: unregistered handler '%s' for vector %u",
             handlers[vector] ? handlers[vector]->name() : "<null>", vector);
    handlers[vector] = nullptr;

    clear_eoi(vector);
}

void InterruptDispatcher::map_pci_irq(uint32_t gsi, uint8_t vector, IInterruptHandler* handler,
                                      uint32_t dest_cpu, bool eoi_first) {
    // Install the handler first so that any subsequent interrupt
    // delivered via this vector has somewhere to go.
    register_handler(vector, handler, eoi_first);

    // PCI/MSI-style interrupts are typically level-triggered and active-low.
    size_t flags = IOAPIC_TRIGGER_LEVEL | IOAPIC_POLARITY_LOW;
    hal::IOAPIC::route_gsi(gsi, vector, dest_cpu, flags | IOAPIC_DELIVERY_FIXED);

    LOG_INFO("IDT: mapped PCI GSI %u -> vector %u CPU %u", gsi, vector, dest_cpu);
}

void InterruptDispatcher::map_legacy_irq(uint8_t irq, uint8_t vector, IInterruptHandler* handler,
                                         uint32_t dest_cpu, bool eoi_first) {
    register_handler(vector, handler, eoi_first);
    hal::IOAPIC::route_legacy_irq(irq, vector, dest_cpu);

    LOG_INFO("IDT: mapped legacy IRQ %u -> vector %u CPU %u", irq, vector, dest_cpu);
}

void InterruptDispatcher::unmap_legacy_irq(uint8_t irq, uint8_t vector) {
    hal::IOAPIC::mask_legacy_irq(irq);
    unregister_handler(vector);
    LOG_INFO("IDT: unmapped legacy IRQ %u from vector %u", irq, vector);
}

void InterruptDispatcher::unmap_pci_irq(uint32_t gsi, uint8_t vector) {
    hal::IOAPIC::mask_gsi(gsi);
    unregister_handler(vector);
    LOG_INFO("IDT: unmapped PCI GSI %u from vector %u", gsi, vector);
}

void InterruptDispatcher::dispatch(TrapFrame* frame) {
    uint8_t vector       = static_cast<uint8_t>(frame->vector);
    PerCPUData* cpu      = CPUCoreManager::get_curr_cpu();
    const bool eoi_first = get_eoi(vector);
    const bool eoi       = (vector >= PLATFORM_INTERRUPT_BASE);

    // ACPI spurious interrupts (often vector 0xFF) are ignored by design:
    // they signal an edge that did not correspond to a real device event.
    if (vector == ACPI_SPURIOUS_INTERRUPT) {
        LOG_DEBUG("IDT: spurious interrupt on vector 0x%02x ignored", vector);
        return;
    }

    if (handlers[vector]) {
        if (eoi && eoi_first) {
            send_eoi(vector);
        }

        IrqStatus status = handlers[vector]->handle(frame);

        if (status == IrqStatus::Unhandled) {
            PANIC("IDT: vector %u was unhandled on CPU %u", vector, cpu->cpu_id);
        }

        if (status == IrqStatus::Reschedule) {
            LOG_DEBUG("IDT: vector %u requested reschedule on CPU %u", vector, cpu->cpu_id);
            task::Scheduler& sched = task::Scheduler::get();
            sched.schedule();
        }
    } else {
        default_handler(frame, cpu->cpu_id);
    }

    // EOIs are only sent for external/IRQ vectors, not for CPU exceptions.
    if (eoi && !eoi_first) {
        send_eoi(vector);
    }
}

void InterruptDispatcher::default_handler(TrapFrame* frame, uint32_t cpu_id) {
    if (frame->vector < PLATFORM_INTERRUPT_BASE) {
        // Exceptions without explicit handlers are treated as fatal; the
        // kernel cannot safely continue from an unknown fault.
        frame->print();

        if (frame->vector == 14) {
            LOG_ERROR("CR2 = 0x%lx", kernel::arch::Cr2::read().linear_address);
        }

        PANIC("[CPU %d] FATAL EXCEPTION: Vector %lu Error %lu", cpu_id, frame->vector,
              frame->error_code);
    } else {
        // External interrupt with no handler: log and continue, but keep
        // going to avoid bringing the system down for a missing driver.
        LOG_WARN("[CPU %d] Unhandled interrupt: Vector %lu", cpu_id, frame->vector);
    }
}
}  // namespace kernel::cpu::arch