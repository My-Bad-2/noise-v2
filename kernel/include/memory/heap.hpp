#pragma once

#include <cstdint>
#include <cstddef>
#include "libs/spinlock.hpp"
#include "memory/memory.hpp"

namespace kernel::memory {

void kfree(void* ptr);
void* kmalloc(size_t size);
}  // namespace kernel::memory