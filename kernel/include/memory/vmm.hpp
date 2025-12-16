#pragma once

#include <cstdint>
#include <cstddef>
#include "memory/pagemap.hpp"

namespace kernel::memory {
struct VmFreeRegion {
    uintptr_t start;     ///< Start of the free virtual range (inclusive).
    size_t length;       ///< Length of the free range in bytes.
    VmFreeRegion* next;  ///< Next region in the sorted free list.
};

class VirtualAllocator {
   public:
    void init(uintptr_t start, size_t length);

    uintptr_t alloc_region(size_t size, size_t align);
    void free_region(uintptr_t start, size_t size);

   private:
    VmFreeRegion* new_node();
    void return_node(VmFreeRegion* node);
    void expand_pool();

    VmFreeRegion* region_head     = nullptr;  ///< Head of the sorted free-region list.
    VmFreeRegion* free_nodes_head = nullptr;  ///< Head of the free-node pool.
};

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

class CowManager {
   public:
    static void init();
    static uintptr_t get_zero_page_phys();
    static bool is_zero_page(uintptr_t virt_addr, PageMap* map);
    static bool handle_fault(uintptr_t virt_addr, PageMap* map);

    static bool initialized() {
        return zero_page_phys == 0;
    }

   private:
    static uintptr_t zero_page_phys;
};
}  // namespace kernel::memory