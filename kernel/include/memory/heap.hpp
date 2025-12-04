/**
 * @file heap.hpp
 * @brief Kernel heap data structures (slab allocator + large-page allocs).
 *
 * The kernel heap is split into two layers:
 *
 *  - **Slab allocator (`SlabCache`)**:
 *      Designed for small, frequently allocated objects (structs,
 *      buffers) of fixed sizes. It amortizes metadata overhead and
 *      reduces external fragmentation by managing objects in slabs
 *      carved out of pages.
 *
 *  - **Big allocations (`BigAllocHeader`)**:
 *      For large or irregular-sized requests, the heap bypasses the
 *      slab layer and directly allocates contiguous pages from the VMM.
 *      A small header is stored before the user pointer to track how
 *      many pages to free later.
 *
 * `KernelHeap` ties these together and exposes a simple `malloc`/`free`
 * interface suitable for internal kernel use.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include "libs/spinlock.hpp"
#include "memory/memory.hpp"

namespace kernel::memory {
/**
 * @brief Metadata header for large (page-based) kernel heap allocations.
 *
 * When a request is too big or does not fit any slab cache, the heap
 * allocates whole pages directly via the virtual memory manager.
 * `BigAllocHeader` is placed in front of the user-visible pointer to
 * remember:
 *  - How many pages were reserved.
 *  - What page size was used (4K/2M/1G).
 *
 * Design reason:
 *  - Avoid forcing the slab layer to handle arbitrarily large/odd-sized
 *    objects, which would fragment slabs and complicate free logic.
 */
struct alignas(16) BigAllocHeader {
    uint32_t magic;       ///< Guard/magic to sanity-check large frees.
    uint32_t page_count;  ///< Number of pages backing this allocation.
    PageSize size_type;   ///< Page granularity used for this allocation.
    uint8_t padding[7];
};

/**
 * @brief Per-slab metadata for fixed-size object caching.
 *
 * Each `SlabHeader` describes one slab: a contiguous region of memory
 * split into fixed-size objects owned by a `SlabCache`.
 *
 * Design goals:
 *  - Track usage at the slab granularity (used/total objects) so that
 *    the cache can keep a pool of partially used slabs hot, and free
 *    completely empty slabs when memory pressure rises.
 *  - Keep a simple free-list pointer for fast O(1) alloc/free within
 *    a slab.
 */
struct SlabHeader {
    SlabHeader* next;
    SlabHeader* prev;
    void* free_list;
    uint16_t used_count;
    uint16_t total_objects;
    uint16_t object_size;
    uint16_t color_offset;
    uint32_t magic;

    // NOLINTNEXTLINE
    void init(size_t obj_size, size_t color);
};

/**
 * @brief Fixed-size object cache backed by slabs.
 *
 * A `SlabCache` specializes in objects of a single size class. It keeps
 * three doubly-linked lists of slabs:
 *  - `partial_head`: slabs with some free objects.
 *  - `full_head`: slabs currently fully utilized.
 *  - `free_head`/`free_tail`: slabs that are entirely free and can be
 *    reclaimed under memory pressure.
 *
 * Design motivations:
 *  - Reduce fragmentation compared to a naive bump allocator by grouping
 *    same-sized objects together.
 *  - Provide predictable allocation time by separating the slow path
 *    (growing slabs) from the fast path (allocating from an existing
 *    slab's freelist).
 */
class SlabCache {
   public:
    /// Initialize this cache to serve a particular object size.
    void init(size_t size);

    /// Allocate one object from this cache (or grow slabs if needed).
    void* alloc();

    /// Return an object to its owning slab and possibly move slab between lists.
    void free(SlabHeader* slab, void* ptr);

    /// Reclaim excess free slabs to keep memory usage bounded.
    void reap();

   private:
    /// Allocate one object from an existing slab's freelist.
    void* alloc_from_slab(SlabHeader* slab);
    /// Grow the cache by creating a new slab and allocate from it.
    void* grow_and_alloc();

    /// Unlink `node` from the given list head.
    void unlink(SlabHeader*& head, SlabHeader* node);
    /// Insert `node` at the head of the list.
    void push(SlabHeader*& head, SlabHeader* node);

    /// Append a slab to the free-slab queue.
    void push_free_node(SlabHeader* node);
    /// Remove a slab from the free-slab queue.
    void remove_free_node(SlabHeader* node);

    SlabHeader* partial_head = nullptr;
    SlabHeader* full_head    = nullptr;
    SlabHeader* free_head    = nullptr;
    SlabHeader* free_tail    = nullptr;
    SpinLock lock;

    size_t obj_size;    ///< Object size this cache manages.
    size_t curr_color;  ///< Current cache coloring offset for cacheline spreading.
    size_t max_color;   ///< Max coloring offset before wrapping.

    size_t free_count     = 0;  ///< Number of fully free slabs tracked.
    size_t max_free_slabs = 0;  ///< Upper bound on cached free slabs.
};

/**
 * @brief Global kernel heap front-end.
 *
 * `KernelHeap` owns an array of `SlabCache` instances for a range of
 * small/medium object sizes plus a fallback path for large allocations.
 *
 * Policy:
 *  - Small allocations are rounded up to the nearest size class and
 *    handled by the corresponding slab cache.
 *  - Large allocations that do not fit any cache are served from the
 *    VMM, with `BigAllocHeader` stored ahead of the user pointer.
 *  - `malloc`/`free` route requests to the appropriate path; callers
 *    need not care about slabs vs big allocs.
 */
class KernelHeap {
   public:
    /// Initialize all slab caches and any global heap state.
    void init();

    /// Allocate `size` bytes from the kernel heap.
    void* malloc(size_t size);

    /// Free a pointer previously returned by `malloc`.
    void free(void* ptr);

    /// Singleton accessor for the global kernel heap.
    static KernelHeap& instance();

   private:
    /// Map an allocation size to a slab cache index.
    static inline int get_index(size_t size);

    /// Handle large allocations that bypass the slab caches.
    void* alloc_large(size_t size);
    /// Free a large allocation using its header metadata.
    void free_large(BigAllocHeader* header);

    /// Array of slab caches for different size classes.
    SlabCache caches[12];
};
}  // namespace kernel::memory