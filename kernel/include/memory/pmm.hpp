#pragma once

#include <cstddef>

namespace kernel::memory {
constexpr size_t CACHE_SIZE = 512;  // 2MB cache per CPU
constexpr size_t BATCH_SIZE = 256;  // Transfer 1MB at a time between Global <-> Local CPU cache

struct PMMStats {
    size_t total_memory;  ///< Total managed physical memory (bytes).
    size_t used_memory;   ///< Bytes currently allocated.
    size_t free_memory;   ///< Bytes currently free.
};

class PhysicalManager {
   public:
    struct PerCPUCache;

    static void init();

    static void* alloc(size_t count = 1);
    static void* alloc_aligned(size_t count, size_t alignment);
    static void* alloc_clear(size_t count = 1);
    static void* alloc_dma(size_t count, size_t alignment);

    static void free(void* ptr, size_t count = 1);
    static void reclaim_type(size_t memmap_type);

    static PMMStats get_stats();

   private:
    // Bitmap helpers: manage page allocation state. Each bit corresponds
    // to a physical page; 1 means "allocated", 0 means "free".
    static void set_bit(size_t idx);
    static void clear_bit(size_t idx);
    static bool test_bit(size_t idx);

    // Walk and modify the bitmap to allocate or free ranges of pages.
    static void* alloc_from_bitmap(size_t count);
    static void free_to_bitmap(size_t page_idx, size_t count);

    // CPU cache helpers for fast single-page alloc/free. This layer sits
    // above the bitmap and is completely transparent to callers.
    static void cache_refill(PerCPUCache& cache);
    static void cache_flush(PerCPUCache& cache);
};

}  // namespace kernel::memory