#pragma once

#include <cstdint>
#include <cstddef>
#include "memory/pagemap.hpp"

namespace kernel::memory {
class VirtualManager {
   public:
    static void init();

    static void* allocate(size_t count, PageSize size = PageSize::Size4K,
                          uint8_t flags = Read | Write, CacheType cache = CacheType::WriteBack);
    static void free(void* ptr, size_t count, PageSize size = PageSize::Size4K,
                     bool free_phys = true);

    // Returns an unmapped, aligned virtual region that callers can
    // manually map to device physical addresses. Only the virtual space
    // is managed here.
    static void* reserve_mmio(size_t size, size_t align);

    static PageMap* curr_map();

   private:
    static void map_pagemap();
    static void map_kernel();
};
}  // namespace kernel::memory