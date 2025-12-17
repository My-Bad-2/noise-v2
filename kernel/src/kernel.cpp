#include "arch.hpp"
#include "hal/acpi.hpp"
#include "libs/log.hpp"
#include "memory/memory.hpp"
#include "hal/cpu.hpp"
#include "task/process.hpp"
#include "hal/timer.hpp"

namespace kernel {
extern "C" void kmain() {
    arch::get_kconsole()->init(115200);

    memory::init();
    hal::ACPI::bootstrap();
    task::Process::init();
    arch::init();

    cpu::CPUCoreManager::init_core(0, 0);
    hal::Timer::init();

    LOG_INFO("Hello, World!");
}
}  // namespace kernel
