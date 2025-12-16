#pragma once

#include <cstdint>
#include <cstddef>
#include "libs/spinlock.hpp"

namespace kernel::memory {
struct BlockHeader {
    uint32_t magic;      ///< Magic tag to sanity-check blocks.
    uint32_t is_free;    ///< Non-zero when block is on the free list.
    size_t size;         ///< Usable payload size in bytes (after header).
    size_t region_size;  ///< Underlying page size used for this region.

    BlockHeader* next;       ///< Next block in physical address order.
    BlockHeader* prev;       ///< Previous block in physical address order.
    BlockHeader* next_free;  ///< Next block in free-list.
    BlockHeader* prev_free;  ///< Previous block in free-list.
};

class KernelHeap {
   public:
    void* alloc(size_t size);
    void free(void* ptr);

   private:
    static size_t align(size_t n);

    void insert_free_node(BlockHeader* block);
    void remove_free_node(BlockHeader* block);

    bool expand_heap(size_t size_needed);

    void coalesce(BlockHeader* block);
    void try_free_region(BlockHeader* block);

    SpinLock lock;
    BlockHeader* free_list_head = nullptr;
};

void* kmalloc(size_t size);
void kfree(void* ptr);
void* aligned_kalloc(size_t size, size_t alignment);
void aligned_kfree(void* ptr);
}  // namespace kernel::memory