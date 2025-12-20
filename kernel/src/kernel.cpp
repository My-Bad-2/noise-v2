#include "hal/acpi.hpp"
#include "hal/smp_manager.hpp"
#include "libs/log.hpp"
#include "task/process.hpp"

extern "C" uint8_t kernel_stack[KSTACK_SIZE] = {};

namespace kernel {
extern "C" void kmain() {
    void* bsp_stack_top = reinterpret_cast<void*>(kernel_stack + KSTACK_SIZE);

    arch::get_kconsole()->init(115200);

    memory::init();
    hal::ACPI::bootstrap();
    task::Process::init();
    arch::init();

    LOG_INFO("Hello, World!");

    void* user_test = task::kernel_proc->mmap(2, memory::PageSize::Size4K,
                                              memory::Read | memory::Write | memory::Execute);

    LOG_DEBUG("user_test = %p", user_test);
    cpu::CpuCoreManager::get().init(bsp_stack_top);
}
}  // namespace kernel