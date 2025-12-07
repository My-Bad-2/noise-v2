#include "arch.hpp"
#include "hal/acpi.hpp"
#include "libs/log.hpp"
#include "memory/memory.hpp"
#include "hal/cpu.hpp"

namespace kernel {
namespace {
hal::IUART* kconsole = nullptr;
}  // namespace

extern "C" void kmain() {
    kconsole = arch::get_kconsole();
    kconsole->init(115200);

    memory::init();
    hal::ACPI::bootstrap();
    arch::init();
    cpu::CPUCoreManager::init_core(0, 0);
    LOG_INFO("Hello, World!");

    arch::halt(true);
}
}  // namespace kernel
