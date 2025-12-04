#pragma once

#include <cstdint>
#include <cstddef>
#include "libs/spinlock.hpp"

namespace kernel::memory {

/**
 * @brief Per-block header used by the kernel heap allocator.
 *
 * Every allocation managed by `KernelHeap` is preceded in memory by a
 * `BlockHeader`. The allocator keeps a doubly-linked list of physical
 * regions (via `next`/`prev`) and a separate free-list (via
 * `next_free`/`prev_free`). The user pointer returned by `kmalloc`
 * points just after this header.
 *
 * Design motivations:
 *  - **Boundary-tag style**: size and free state live next to the data,
 *    which makes splitting and coalescing blocks cheap and local.
 *  - **Magic value**: a simple guard (`magic`) allows basic detection
 *    of heap corruption or invalid frees.
 *  - **Region size tracking**: `region_size` stores the page size used
 *    when the region was obtained from the VMM, so the heap can decide
 *    when a fully-free region can be returned to the system.
 */
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

/**
 * @brief Simple best-fit kernel heap on top of the VMM.
 *
 * `KernelHeap` implements a single global heap used by `kmalloc`/`kfree`.
 * It carves memory out of virtual regions obtained from
 * `VirtualManager::allocate` and organizes them into a linked list of
 * variable-sized blocks.
 *
 * Policy and trade-offs:
 *  - **Best fit**: the allocator scans the free list and chooses the
 *    smallest block that satisfies the request. This tends to preserve
 *    large free regions and limit fragmentation in steady state, while
 *    remaining conceptually simple.
 *  - **Alignment to 16 bytes**: all user allocations are rounded up and
 *    aligned to 16 bytes. This matches typical ABI alignment guarantees
 *    and reduces the chance of false sharing on cache lines.
 *  - **On-demand heap growth**: when no suitable block is found, the
 *    heap grows by requesting more pages from the VMM. The growth size
 *    is biased toward large pages (2 MiB / 1 GiB) when possible to keep
 *    page-table pressure low.
 *  - **Coalescing + region release**: adjacent free blocks are merged,
 *    and if an entire VMM-managed region becomes one big free block, it
 *    is returned to the VMM, preventing unbounded heap growth.
 *
 * This design is intentionally minimal: it favors straightforward
 * reasoning and debuggability over highly-tuned performance tricks.
 */
class KernelHeap {
   public:
    /**
     * @brief Allocate `size` bytes from the kernel heap.
     *
     * The returned pointer is at least 16-byte aligned and comes from a
     * best-fit block inside the heap. On failure, nullptr is returned.
     */
    void* alloc(size_t size);

    /**
     * @brief Free a pointer previously returned by `alloc`/`kmalloc`.
     *
     * The block is marked as free, placed back on the free list, then
     * coalesced with adjacent free neighbors. If an entire region
     * becomes free, it may be released back to the VMM.
     */
    void free(void* ptr);

    /**
     * @brief Align a raw size to the internal heap alignment.
     *
     * Exposed as a helper for tests or low-level callers that need to
     * know the allocator's rounding behavior.
     */
    static size_t align(size_t n);

   private:
    /// Insert a block into the free-list (at the head).
    void insert_free_node(BlockHeader* block);
    /// Remove a block from the free-list.
    void remove_free_node(BlockHeader* block);

    /// Grow the heap by requesting a new region from the VMM.
    bool expand_heap(size_t size_needed);

    /**
     * @brief Attempt to merge a free block with its neighbors.
     *
     * Coalescing is key to controlling fragmentation. After merging,
     * `try_free_region` is consulted to see if a whole region can be
     * returned to `VirtualManager`.
     */
    void coalesce(BlockHeader* block);

    /**
     * @brief Return a fully free region to the VMM if possible.
     *
     * When a block represents an entire region (no neighbors), the heap
     * chooses to release it, shrinking its footprint and leaving page-
     * level placement decisions to the VMM/PMM.
     */
    void try_free_region(BlockHeader* block);

    SpinLock lock;
    BlockHeader* free_list_head = nullptr;
};

/// Global helpers that route to the single kernel heap instance.
void* kmalloc(size_t size);
void kfree(void* ptr);

}  // namespace kernel::memory