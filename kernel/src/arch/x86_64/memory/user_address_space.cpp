#include "cpu/registers.hpp"
#include "hal/interrupt.hpp"
#include "hal/smp_manager.hpp"
#include "libs/log.hpp"
#include "memory/memory.hpp"
#include "memory/vma.hpp"
#include "hal//interface/interrupt.hpp"

#define PF_PRESENT           0x01
#define PF_WRITE             0x02
#define PF_USER              0x04
#define PF_RESERVED_WRITE    0x08
#define PF_INSTRUCTION_FETCH 0x10
#define PF_PROTECTION_KEY    0x20
#define PF_SHADOW_STACK      0x40
#define PF_SGX               0x8000

namespace kernel::memory {
class PageFaultHandler : public cpu::IInterruptHandler {
   public:
    const char* name() const {
        return "Page Fault Handler!";
    }

    cpu::IrqStatus handle(cpu::arch::TrapFrame* frame) {
        arch::Cr2 cr2         = arch::Cr2::read();
        cpu::PerCpuData* cpu  = cpu::CpuCoreManager::get().get_current_core();
        UserAddressSpace& uas = cpu->curr_thread->owner->vma;

        if (!uas.handle_page_fault(cr2.linear_address, frame->error_code)) {
            PANIC("Page Fault Not handled");
            return cpu::IrqStatus::Unhandled;
        }

        return cpu::IrqStatus::Handled;
    }
};

bool UserAddressSpace::handle_page_fault(uintptr_t fault_addr, size_t error_code) {
    LockGuard guard(this->mutex);

    UserVmRegion* region = this->find_region_containing(fault_addr);

    LOG_DEBUG("PF Handler Called!");

    if (!region) {
        return false;
    }

    if ((error_code & PF_WRITE) && !(region->flags & Write)) {
        LOG_ERROR("Write violation!");
        return false;
    }

    if ((error_code & PF_USER) && !(region->flags & User)) {
        LOG_ERROR("Privilege violation!");
        return false;
    }

    size_t alignment = PAGE_SIZE_4K;

    if (region->page_size == PageSize::Size1G) {
        alignment = PAGE_SIZE_1G;
    } else if (region->page_size == PageSize::Size2M) {
        alignment = PAGE_SIZE_2M;
    }

    uintptr_t page_base = fault_addr & ~(alignment - 1);

    if (this->page_map->translate(page_base) != 0) {
        return true;
    }

    if (!this->page_map->map(page_base, region->flags, region->cache, region->page_size)) {
        LOG_ERROR("Out of memory!");
        return false;
    }

    return true;
}

void UserAddressSpace::arch_init() {
    static PageFaultHandler pf_handler;
    cpu::arch::InterruptDispatcher::register_handler(EXCEPTION_PAGE_FAULT, &pf_handler);
}
}  // namespace kernel::memory