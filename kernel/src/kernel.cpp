#include "arch.hpp"


namespace kernel {
namespace {
extern "C" [[gnu::used]] uint8_t kstack[KSTACK_SIZE] = {};
hal::IUART* kconsole = nullptr;
}

extern "C" void kmain() {
    arch::init();

    kconsole = arch::get_kconsole();
    kconsole->init(115200);
    kconsole->send_string("Hello, World!\n");
}
}  // namespace kernel
