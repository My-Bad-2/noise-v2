/**
 * @file vmm.hpp
 * @brief Kernel virtual memory management interfaces.
 *
 * Defines:
 *  - `VmFreeRegion`: a node describing free virtual ranges.
 *  - `VirtualAllocator`: a free-list-based allocator for virtual address
 *    space (no physical memory involved).
 *  - `VirtualManager`: high-level helpers to build and use the kernel
 *    address space on top of `PageMap` and `VirtualAllocator`.
 *
 * In the overall architecture:
 *  - `PhysicalManager` owns physical pages.
 *  - `PageMap` describes mappings between virtual and physical pages.
 *  - `VirtualAllocator` and `VirtualManager` decide how the kernel uses
 *    its virtual address space for heaps, MMIO, and other regions.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include "memory/pagemap.hpp"

namespace kernel::memory {
/**
 * @brief Singly-linked free-list node for virtual address ranges.
 *
 * Each `VmFreeRegion` describes a contiguous region of *unmapped*
 * virtual address space that is available for allocation by the kernel.
 *
 * Design notes:
 *  - Regions are kept sorted by `start` so that neighbors can be
 *    coalesced cheaply when ranges are freed.
 *  - The allocator operates only on virtual ranges; physical memory is
 *    acquired separately via `PageMap`/`PhysicalManager`.
 */
struct VmFreeRegion {
    uintptr_t start;      ///< Start of the free virtual range (inclusive).
    size_t length;        ///< Length of the free range in bytes.
    VmFreeRegion* next;   ///< Next region in the sorted free list.
};

/**
 * @brief Kernel virtual address space allocator.
 *
 * `VirtualAllocator` is responsible for sub-allocating ranges from a
 * larger virtual arena (e.g. the "kernel heap" area above all RAM).
 * It does *not* allocate physical pages; it only tracks which virtual
 * addresses are free or in use.
 *
 * Strategy:
 *  - Maintains a sorted singly-linked list of `VmFreeRegion` nodes that
 *    cover all free virtual regions.
 *  - Uses a simple first-fit allocation algorithm with alignment
 *    support, splitting regions as needed to satisfy requests.
 *  - To avoid per-node heap allocations, metadata nodes are carved out
 *    of pages allocated from the PMM and recycled via a node pool.
 *
 * This separation allows the kernel to reason about virtual layout
 * (where things live) independently from how they are backed in RAM.
 */
class VirtualAllocator {
   public:
    /**
     * @brief Initialize the allocator with a single free region.
     *
     * @param start  Base virtual address of the managed arena.
     * @param length Size of the arena in bytes.
     *
     * Typically called once during boot to set up the kernel's virtual
     * heap area, which will then be carved into smaller allocations.
     */
    void init(uintptr_t start, size_t length);

    /**
     * @brief Allocate a contiguous virtual address region.
     *
     * @param size  Requested size in bytes.
     * @param align Alignment requirement in bytes (power-of-two).
     *
     * @return Starting virtual address of the reserved range, or 0 on
     *         failure. The region is *unmapped*; callers must map it.
     */
    uintptr_t alloc_region(size_t size, size_t align);

    /**
     * @brief Free a previously reserved virtual region.
     *
     * @param start Starting virtual address of the region.
     * @param size  Size of the region in bytes (must match allocation).
     *
     * The freed region is merged with adjacent free regions when
     * possible to combat fragmentation.
     */
    void free_region(uintptr_t start, size_t size);

   private:
    /// Acquire a fresh metadata node from the internal node pool.
    VmFreeRegion* new_node();
    /// Return a metadata node back to the internal pool for reuse.
    void return_node(VmFreeRegion* node);
    /// Grow the node pool by allocating another page of `VmFreeRegion`s.
    void expand_pool();

    VmFreeRegion* region_head     = nullptr;  ///< Head of the sorted free-region list.
    VmFreeRegion* free_nodes_head = nullptr;  ///< Head of the free-node pool.
};

/**
 * @brief High-level virtual memory manager facade.
 *
 * `VirtualManager` coordinates:
 *  - Construction of the kernel address space (`map_pagemap`, `map_kernel`)
 *    on top of a `PageMap`.
 *  - Reservation and mapping of virtual memory for the kernel via
 *    `VirtualAllocator` and `PageMap`.
 *
 * Conceptually:
 *  - `VirtualAllocator` decides *which* virtual addresses to use.
 *  - `PageMap` decides *how* those addresses are translated to physical.
 *  - `VirtualManager` glues the two together for typical kernel use.
 */
class VirtualManager {
   public:
    /**
     * @brief Build the higher-half direct map from the Limine memmap.
     *
     * This sets up the "direct map" where physical memory is mirrored in
     * the higher-half virtual address space using large pages where
     * possible. It forms the foundation for the rest of the kernel's
     * virtual mappings.
     */
    static void map_pagemap();

    /**
     * @brief Map the kernel ELF image into the new virtual address space.
     *
     * Uses the ELF headers provided by the bootloader to map each
     * loadable segment with appropriate permissions, making the kernel
     * executable from its linked virtual base.
     */
    static void map_kernel();

    /**
     * @brief Initialize the kernel's virtual memory layout and activate it.
     *
     * Performs:
     *  - Global paging setup (via `PageMap::global_init()`).
     *  - Higher-half direct mapping of physical memory.
     *  - Kernel ELF mapping.
     *  - Initialization of the kernel's virtual heap arena.
     *
     * After this call, `allocate`/`free` can be used for general-purpose
     * kernel virtual memory allocation.
     */
    static void init();

    /**
     * @brief Allocate and map pages into kernel virtual space.
     *
     * @param count Number of pages of the given size to allocate.
     * @param size  Page granularity (4K/2M/1G).
     * @param flags Logical access flags (Read/Write/User/Execute/Global).
     * @param cache Cache policy for the mapping (e.g. WriteBack, WC).
     *
     * The function:
     *  1. Reserves a virtual region using `VirtualAllocator`.
     *  2. Backs each page with physical memory via `PageMap::map`.
     *
     * On failure, it rolls back both the virtual reservation and any
     * partially created mappings.
     */
    static void* allocate(size_t count, PageSize size = PageSize::Size4K,
                          uint8_t flags = Read | Write, CacheType cache = CacheType::WriteBack);

    /**
     * @brief Unmap and free a previously allocated region.
     *
     * Tears down mappings (optionally freeing physical pages) and
     * returns the virtual range to the allocator.
     */
    static void free(void* ptr, size_t count, PageSize size = PageSize::Size4K);

    /**
     * @brief Reserve virtual address space for MMIO mappings.
     *
     * Returns an unmapped, aligned virtual region that callers can
     * manually map to device physical addresses. Only the virtual space
     * is managed here.
     */
    static void* reserve_mmio(size_t size, size_t align);
};
}  // namespace kernel::memory