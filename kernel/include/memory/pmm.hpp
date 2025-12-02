/**
 * @file pmm.hpp
 * @brief Physical memory manager (PMM) interface.
 *
 * Exposes a page-based physical memory allocator for the kernel. All
 * allocations and frees operate on 4 KiB pages (`PMM_PAGE_SIZE`).
 *
 * In the overall architecture, this is the *bottom-most* allocator:
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
 * This design gives:
 *  - **Fast common case**: single-page operations are often served directly
 *    from the stack cache.
 *  - **Efficient global search**: the summary bitmap lets the allocator
 *    skip large spans of fully-used memory during bitmap scans.
 *  - **Compact representation**: the bitmaps still provide a compact,
 *    unified view of all physical memory.
 */

#pragma once

#include <cstddef>

namespace kernel::memory {

/// Size of a single physical page (4 KiB). Used throughout the kernel.
constexpr size_t PMM_PAGE_SIZE = 0x1000;

/// Number of pages kept in the quick free-page stack cache. This cache
/// accelerates frequent single-page alloc/free patterns commonly seen
/// in page-table management and small kernel allocations.
constexpr size_t CACHE_SIZE = 512;  // Keep 512 pages in the quick stack

/**
 * @brief Aggregate statistics reported by the physical memory manager.
 *
 * These stats are useful for diagnostics and monitoring memory usage
 * across the entire system.
 */
struct PMMStats {
    size_t total_memory;  ///< Total managed physical memory (bytes).
    size_t used_memory;   ///< Bytes currently allocated.
    size_t free_memory;   ///< Bytes currently free.
};

/**
 * @brief Static physical memory manager.
 *
 * Provides page-based allocation, aligned allocation, zeroed pages, and
 * a quick stack cache for single-page operations.
 *
 * In the architecture:
 *  - `kernel::memory::init()` sets up the higher-half mapping and then
 *    calls `PhysicalManager::init()`.
 *  - All physical memory consumers (page tables, frame allocators, etc.)
 *    obtain raw physical frames via this class.
 */
class PhysicalManager {
   public:
    /**
     * @brief Initialize the physical memory manager.
     *
     * Must be called once early during boot, after the bootloader memory
     * map is available. It:
     *  - Parses the Limine memory map.
     *  - Reserves space for internal metadata (bitmap + cache).
     *  - Marks all pages as used, then frees back the usable ones.
     */
    static void init();

    /**
     * @brief Allocate one or more contiguous physical pages.
     *
     * This is the primary entry point for requesting physical frames.
     *
     * @param count Number of pages to allocate (default: 1).
     * @return Physical address (as void*) on success, nullptr on failure.
     */
    static void* alloc(size_t count = 1);

    /**
     * @brief Allocate contiguous physical pages with alignment constraint.
     *
     * Typically used for structures that must be aligned to large
     * boundaries (e.g. paging structures, DMA buffers).
     *
     * @param count     Number of pages to allocate.
     * @param alignment Required alignment in bytes (multiple of PMM_PAGE_SIZE).
     * @return Physical address on success, nullptr on failure.
     */
    static void* alloc_aligned(size_t count, size_t alignment);

    /**
     * @brief Allocate and clear pages.
     *
     * Like `alloc`, but the returned memory is zero-filled. This is
     * handy when backing page tables or zeroing out metadata.
     */
    static void* alloc_clear(size_t count = 1);

    /**
     * @brief Free previously allocated pages.
     *
     * This returns pages back to the PMM pool. For single-page frees, a
     * small stack cache is used for better locality and performance.
     *
     * @param ptr   Physical address returned by `alloc`/`alloc_aligned`.
     * @param count Number of pages in the block (default: 1).
     */
    static void free(void* ptr, size_t count = 1);

    /**
     * @brief Reclaim all pages of a given Limine memmap type.
     *
     * After the kernel has finished using bootloader, ACPI, or module
     * memory, it can call this function to return such regions to the
     * general-purpose physical memory pool.
     */
    static void reclaim_type(size_t memmap_type);

    /**
     * @brief Get current PMM statistics.
     *
     * This is safe to call at any time after initialization and provides
     * a snapshot of physical memory usage.
     */
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

    // Stack cache helpers for fast single-page alloc/free. This layer sits
    // above the bitmap and is completely transparent to callers.
    static bool cache_push(void* addr);
    static void* cache_pop();
    static void flush_cache_to_bitmap();
};

}  // namespace kernel::memory