#include "arch.hpp"
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

    arch::init();
    memory::init();
    cpu::CPUCoreManager::init_core(0, 0);

    LOG_DEBUG("Hello, World!");

    arch::halt(true);
}
}  // namespace kernel
