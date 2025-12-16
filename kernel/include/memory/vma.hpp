#pragma once

#include <cstdint>
#include <cstddef>

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
}  // namespace kernel::memory