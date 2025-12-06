#include "arch.hpp"
#include "hal/acpi.hpp"
#include "libs/log.hpp"
#include "memory/memory.hpp"
#include "hal/cpu.hpp"
#include "boot/boot.h"

namespace kernel {
namespace {
hal::IUART* kconsole = nullptr;
}  // namespace

extern "C" void kmain() {
    kconsole = arch::get_kconsole();
    kconsole->init(115200);

    memory::init();
    arch::init();
    cpu::CPUCoreManager::init_core(0, 0);
    hal::ACPI::init();
    LOG_DEBUG("Hello, World!");

    arch::halt(true);
}
}  // namespace kernel
