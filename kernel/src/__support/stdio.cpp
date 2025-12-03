#include "arch.hpp"

namespace kernel::__support {
// Dummy file descriptor
extern "C" void* __llvm_libc_stdout_cookie = reinterpret_cast<void*>(1);

extern "C" size_t __llvm_libc_stdio_write(void*, const char* data, size_t size) {
    hal::IUART* kconsole = arch::get_kconsole();

    // if (cookie == __llvm_libc_stdout_cookie) {
    for (size_t i = 0; i < size; ++i) {
        kconsole->send_char(data[i]);
    }

    return size;
    // }
}
}  // namespace kernel::__support