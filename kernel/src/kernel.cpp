#include "hal/acpi.hpp"
#include "hal/smp_manager.hpp"

namespace kernel {
extern "C" void kmain() {
    arch::get_kconsole()->init(115200);

    memory::init();
    hal::ACPI::bootstrap();
    task::Process::init();
    arch::init();

    LOG_INFO("Hello, World!");
    cpu::CpuCoreManager::get().init();
}
}  // namespace kernel
