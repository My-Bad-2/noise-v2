#include <stdint.h>
#include <string.h>

extern "C" {
namespace {
[[gnu::used]] uint8_t kstack[KSTACK_SIZE] = {};

void kmain() {
    size_t len = strlen("Hello, World!");
}
}
}