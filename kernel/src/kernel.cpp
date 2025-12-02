#include "arch.hpp"
#include "libs/log.hpp"

namespace kernel {
namespace {
extern "C" [[gnu::used]] uint8_t kstack[KSTACK_SIZE] = {};
hal::IUART* kconsole = nullptr;
}

extern "C" void kmain() {    
    kconsole = arch::get_kconsole();
    kconsole->init(115200);

    arch::init();
    LOG_DEBUG("Hello, World!");

    arch::halt(true);
}
}  // namespace kernel
