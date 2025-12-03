#include "arch.hpp"
#include "libs/log.hpp"
#include "memory/memory.hpp"

namespace kernel {
namespace {
hal::IUART* kconsole = nullptr;
}  // namespace

extern "C" void kmain() {
    kconsole = arch::get_kconsole();
    kconsole->init(115200);

    arch::init();
    memory::init();

    LOG_DEBUG("Hello, World!");

    arch::halt(true);
}
}  // namespace kernel
