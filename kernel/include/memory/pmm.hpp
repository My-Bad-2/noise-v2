/**
 * @file pmm.hpp
 * @brief Physical memory manager (PMM) interface.
 *
 * Exposes a page-based physical memory allocator for the kernel. All
 * allocations and frees operate on 4 KiB pages (`PAGE_SIZE_4K`).
 *
 * In the overall architecture, this is the bottom-most allocator:
 *  - It owns raw physical pages discovered from the bootloader's memory map.
 *  - Higher layers (paging, kernel heap, slab allocators, etc.) build on
 *    top of this interface and usually do not touch physical pages directly.
 *  - The PMM is initialized very early (in `kernel::memory::init()`), so
 *    that virtual memory and other subsystems can request backing pages.
 *
 * ## Internal design (bitmap + summary bitmap + stack cache)
 *
 * The PMM tracks page usage with three cooperating mechanisms:
 *
 * 1. **Global bitmap (authoritative state)**
 *    - There is one bit per physical page.
 *    - Bit value `1`  => page is *allocated / in use*.
 *      Bit value `0`  => page is *free / available*.
 *    - All multi-page allocations and frees ultimately operate on this bitmap.
 *    - This is the ground truth for the allocator.
 *
 *    The choice of a bitmap is about *compactness* and *cache locality*:
 *    memory usage scales linearly with total pages, and scanning over
 *    contiguous physical ranges is friendly to CPU caches.
 *
 * 2. **Summary bitmap (hierarchical fast-skip)**
 *    - Each bit of the summary bitmap represents one *word* in the main
 *      bitmap (i.e. 64 pages).
 *    - Summary bit value `1`  => the corresponding 64-page block in the
 *      main bitmap is completely full (all ones).
 *      Summary bit value `0`  => the block has at least one free page.
 *    - Single-page and multi-page searches can scan the summary bitmap
 *      first to quickly skip over large fully-allocated regions (64 pages
 *      at a time, or even 4096 pages when whole summary words are full).
 *
 *    The summary layer is a *micro index* over the main bitmap: it trades
 *    a small amount of extra metadata for the ability to avoid touching
 *    obviously-full regions at all. This keeps allocation latency more
 *    stable under heavy fragmentation or high occupancy.
 *
 * 3. **Stack cache (fast path for single pages)**
 *    - A small LIFO stack (`CACHE_SIZE` entries) holds physical addresses
 *      of recently freed **single** pages.
 *    - When you `alloc(1)`, the PMM first tries to `cache_pop()` from this
 *      stack; only if it’s empty does it fall back to bitmap/summary search.
 *    - When you `free(ptr, 1)`, the PMM tries to `cache_push(ptr)` into
 *      this stack instead of immediately updating the bitmap. If the stack
 *      is full, it flushes roughly half of the cached pages back into the
 *      bitmap (via `free_to_bitmap`) to free up cache space.
 *
 *    The stack cache exists for *temporal locality*: many kernel patterns
 *    (page table churn, short-lived buffers) free and reallocate the same
 *    number of single pages in bursts. Serving those from a tiny hot
 *    structure avoids repeated bitmap walks and reduces contention on the
 *    global PMM state.
 *
 * This design gives:
 *  - **Fast common case**: single-page operations are often served directly
 *    from the stack cache.
 *  - **Efficient global search**: the summary bitmap lets the allocator
 *    skip large spans of fully-used memory during bitmap scans instead of
 *    linearly probing every word.
 *  - **Compact representation**: the bitmaps still provide a compact,
 *    unified view of all physical memory.
 */

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

    // CPU cache stack helpers for fast single-page alloc/free. This layer sits
    // above the bitmap and is completely transparent to callers.
    static void cache_refill(PerCPUCache& cache);
    static void cache_flush(PerCPUCache& cache);
};

}  // namespace kernel::memory