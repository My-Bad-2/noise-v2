#include "arch.hpp"
#include "cpu/idt.hpp"
#include "cpu/registers.hpp"
#include "hal/interrupt.hpp"
#include "hal/lapic.hpp"
#include "hal/smp_manager.hpp"
#include "cpu/regs.h"
#include "cpu/simd.hpp"
#include "boot/boot.h"
#include "libs/log.hpp"
#include "memory/paging.hpp"

extern "C" void syscall_entry();

namespace kernel::cpu {
namespace {
std::byte* nmi_stack = nullptr;
std::byte* df_stack  = nullptr;

struct TLBRequest {
    uintptr_t start_addr;
    size_t page_count;
};

struct FuncCallRequest {
    void (*func)(void*);
    void* arg;
    uint32_t target_apic_id;
};

volatile TLBRequest tlb_request_mailbox;
volatile FuncCallRequest call_request_mailbox;

std::atomic<size_t> pending_acks(0);
SpinLock smp_lock;

class TlbShootDownHandler : public IInterruptHandler {
   public:
    const char* name() const {
        return "TLB Shootdown";
    }

    IrqStatus handle(arch::TrapFrame* frame) {
        uintptr_t addr = tlb_request_mailbox.start_addr;
        size_t count   = tlb_request_mailbox.page_count;

        for (size_t i = 0; i < count; ++i) {
            memory::TLB::flush(addr + (i * memory::PAGE_SIZE_4K));
        }

        pending_acks.fetch_sub(1, std::memory_order_release);

        return IrqStatus::Handled;
    }
};

class RemoteCallHandler : public IInterruptHandler {
   public:
    const char* name() const {
        return "Function Caller";
    }

    IrqStatus handle(arch::TrapFrame* frame) {
        uint32_t apic_id = cpu::CpuCoreManager::get().get_current_core()->apic_id;

        if (call_request_mailbox.target_apic_id == apic_id) {
            if (call_request_mailbox.func) {
                call_request_mailbox.func(call_request_mailbox.arg);
            }

            // Acknowledge completion
            pending_acks.fetch_sub(1, std::memory_order_release);
        }

        return IrqStatus::Handled;
    }
};

class StopAllCoresHandler : public IInterruptHandler {
   public:
    const char* name() const {
        return "Stop Core";
    }

    IrqStatus handle(arch::TrapFrame* frame) {
        kernel::arch::halt(false);
    }
};

StopAllCoresHandler stop_cores_handler;
RemoteCallHandler remote_call_handler;
TlbShootDownHandler tlb_shootdown_handler;
}  // namespace

void PerCpuData::arch_init() {
    if (nmi_stack == nullptr) {
        nmi_stack = new std::byte[0x1000];
    }

    if (df_stack == nullptr) {
        df_stack = new std::byte[0x1000];
    }

    if (!nmi_stack || !df_stack) {
        PANIC("IST Stack Allocation failed!");
    }

    this->arch.gdt->set_ist(0, reinterpret_cast<uintptr_t>(nmi_stack) + 0x1000);
    this->arch.gdt->set_ist(1, reinterpret_cast<uintptr_t>(df_stack) + 0x1000);

    this->arch.gdt->setup_gdt();
    this->arch.gdt->setup_tss(this->kstack_top);
}

void PerCpuData::commit() {
    this->arch.gdt->load_tables();
    arch::IDTManager::load_table();

    hal::Lapic::init();
    hal::Lapic::calibrate();
    arch::SIMD::init();

    kernel::arch::Msr msr;
    msr.index = MSR_GS_BASE;
    msr.value = reinterpret_cast<uintptr_t>(this);
    msr.write();

    hal::Timer::init();

    kernel::arch::enable_interrupts();

    arch::InterruptDispatcher::register_handler(IPI_TLB_SHOOTDOWN_VECTOR, &tlb_shootdown_handler);
    arch::InterruptDispatcher::register_handler(IPI_FUNCTION_CALL_VECTOR, &remote_call_handler);
    arch::InterruptDispatcher::register_handler(IPI_PANIC_VECTOR, &stop_cores_handler);
}

void CpuCoreManager::ap_main(PerCpuData* data) {
    data->arch.gdt->load_tables();
    arch::IDTManager::load_table();

    hal::Lapic::init();
    hal::Lapic::calibrate();
    arch::SIMD::init();

    kernel::arch::Msr msr;
    msr.index = MSR_GS_BASE;
    msr.value = reinterpret_cast<uintptr_t>(data);
    msr.write();

    hal::Timer::init();

    data->is_online.store(true);
    init_syscalls();
    kernel::arch::enable_interrupts();

    LOG_INFO("AP Core %u (APIC %u) is online!", data->core_idx, data->apic_id);

    kernel::arch::halt(true);
}

void CpuCoreManager::ap_entry_func(limine_mp_info* info) {
    memory::PageMap::get_kernel_map()->load();
    PerCpuData* data = reinterpret_cast<PerCpuData*>(info->extra_argument);

    asm volatile("mov %0, %%rsp" ::"r"(data->kstack_top) : "memory");
    asm volatile("mov %0, %%rdi" ::"r"(data) : "memory");
    asm volatile("call *%0" ::"r"(ap_main) : "memory");
    kernel::arch::halt(true);
}

PerCpuData* CpuCoreManager::get_current_core() {
    PerCpuData* val;
    asm volatile("mov %%gs:0, %0" : "=r"(val));
    return val;
}

PerCpuData::PerCpuData(uint32_t idx, limine_mp_info* info)
    : self(this),
      acpi_id(info->processor_id),
      core_idx(idx),
      apic_id(info->lapic_id),
      pcid_manager(new memory::PcidManager),
      arch() {
    this->is_bsp = (info->lapic_id == mp_request.response->bsp_lapic_id);
    this->is_online.store(this->is_bsp);
}

void CpuCoreManager::send_ipi(uint32_t dest, uint8_t vector) {
    hal::Lapic::send_ipi(dest, vector);
}

bool CpuCoreManager::send_ipi_to_others(uint8_t vector) {
    size_t curr_idx    = cpu::CpuCoreManager::get().get_current_core()->core_idx;
    size_t total_cores = cpu::CpuCoreManager::get().get_total_cores();

    // Single core, nothing to do
    if (total_cores <= 1) {
        return false;
    }

    // Reset ack counter to expected number of responses
    pending_acks.store(total_cores - 1, std::memory_order_release);

    // Send Broadcast IPI (excluding self)
    hal::Lapic::broadcast_ipi(vector, false);
    return true;
}

void CpuCoreManager::wait_for_acks() {
    // Spin until all cores have decremented the counter
    while (pending_acks.load(std::memory_order_acquire) > 0) {
        kernel::arch::pause();
    }
}

void CpuCoreManager::tlb_shootdown(uintptr_t virt_addr) {
    tlb_shootdown(virt_addr, 1);
}

void CpuCoreManager::tlb_shootdown(uintptr_t virt_addr, size_t count) {
    LockGuard guard(smp_lock);

    tlb_request_mailbox.start_addr = virt_addr;
    tlb_request_mailbox.page_count = count;

    if (send_ipi_to_others(IPI_TLB_SHOOTDOWN_VECTOR)) {
        wait_for_acks();
    }
}

void CpuCoreManager::call_on_core(uint32_t core_idx, void (*func)(void*), void* arg) {
    PerCpuData* target_core = get().get_core_by_index(core_idx);
    LOG_DEBUG("Here");

    if (!target_core || !target_core->is_online.load()) {
        return;
    }

    LOG_DEBUG("Here");

    PerCpuData* curr_core = get().get_current_core();

    if (core_idx == curr_core->core_idx) {
        // Just call it if it's us
        func(arg);
        return;
    }

    LockGuard guard(smp_lock);

    call_request_mailbox.func           = func;
    call_request_mailbox.arg            = arg;
    call_request_mailbox.target_apic_id = target_core->apic_id;

    pending_acks.store(1, std::memory_order_release);

    hal::Lapic::broadcast_ipi(IPI_FUNCTION_CALL_VECTOR, false);
    wait_for_acks();
}

void CpuCoreManager::stop_other_cores() {
    hal::Lapic::broadcast_ipi(IPI_PANIC_VECTOR, false);
}

void CpuCoreManager::init_syscalls() {
    using namespace kernel::arch;

    // Enable System Call Extensions (SCR) in EFER
    Msr msr   = Msr::read(MSR_EFER);
    msr.index = MSR_EFER;
    msr.value |= EFER_SCE;
    msr.write();

    // Set up STAR (Segment Selectors)
    // [64:48] User Base = 0x10 (Data: 0x18, Code: 0x20)
    // [47:32] Kernel Base = 0x08 (Kernel Code: 0x08)
    // [31:00] Reserved

    // User Base is 0x10 so that `sysret` can reach `0x18` and `0x20`
    // by adding its fixed offsets.
    // `sysret` calculates SS offset by base + 8
    // `sysret` calculates CS offset by base + 16
    msr.index = MSR_STAR;
    msr.value = (0x10ul << 48) | (0x08ul << 32);
    msr.write();

    // Setup LSTAR (Target RIP for syscall)
    msr.index = MSR_LSTAR;
    msr.value = reinterpret_cast<uintptr_t>(syscall_entry);
    msr.write();

    // Set SFMASK (RFLAGS Mask)
    msr.index = MSR_FMASK;
    // Clear Interrupt flag, Direction Flag, Trap Flag, and Nested Task
    // on syscall
    msr.value = FLAGS_IF | FLAGS_DF | FLAGS_TF | FLAGS_NT;
    msr.write();
}
}  // namespace kernel::cpu