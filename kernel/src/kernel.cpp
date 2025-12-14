#include "arch.hpp"
#include "hal/acpi.hpp"
#include "libs/log.hpp"
#include "memory/memory.hpp"
#include "hal/cpu.hpp"
#include "task/process.hpp"

namespace kernel {
void idle(void*) {
    arch::halt(true);
}

extern "C" void kmain() {
    arch::get_kconsole()->init(115200);

    memory::init();
    hal::ACPI::bootstrap();
    arch::init();
    cpu::CPUCoreManager::init_core(0, 0);

    LOG_INFO("Hello, World!");

    auto thread = new task::Thread(nullptr, idle, nullptr);

    arch::halt(true);
}
}  // namespace kernel
