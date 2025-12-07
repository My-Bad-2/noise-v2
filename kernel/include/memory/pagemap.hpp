/**
 * @file pagemap.hpp
 * @brief Abstraction over x86_64 paging structures.
 *
 * `PageMap` owns a single page-table hierarchy (rooted at CR3) and
 * provides helpers to:
 *  - Map/unmap/translate virtual addresses.
 *  - Map ranges using large pages when possible.
 *  - Load the map into CR3 with optional PCID handling.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <utility>
#include "memory/memory.hpp"

namespace kernel::memory {
enum class CacheType : uint8_t {
    WriteBack,
    Uncached,
    WriteCombining,
    WriteThrough,
    WriteProtected,
};

enum PageFlags : uint8_t {
    Read    = (1 << 0),
    Write   = (1 << 1),
    User    = (1 << 2),
    Execute = (1 << 3),
    Global  = (1 << 4),
    Lazy    = (1 << 5)
};

/**
 * @brief Per-address-space page table abstraction.
 *
 * A `PageMap` conceptually represents one address space:
 *  - It owns a single page-table root (the value that will be loaded
 *    into CR3) and the tree of paging structures below it.
 *  - It knows nothing about *who* uses that address space (kernel vs
 *    user), only how to describe mappings at the page-table level.
 *
 * Design ideas:
 *  - **Separation of concerns**: higher layers (VMM, processes) talk in
 *    terms of virtual ranges and flags; `PageMap` hides the details of
 *    page-table walks, large-page promotion, and TLB maintenance.
 *  - **Greedy large pages**: `map_range` prefers 1 GiB/2 MiB mappings
 *    when alignment and length allow, to reduce TLB pressure and page‑
 *    table depth, but falls back to 4 KiB automatically.
 *  - **Lazy structure allocation**: intermediate page tables are only
 *    allocated when a mapping actually needs them, which keeps paging
 *    structures sparse and reduces physical memory usage.
 *  - **PCID‑aware loading**: `load()` optionally programs PCID fields
 *    in CR3 and can request that hardware preserve TLB entries, making
 *    context switches cheaper on CPUs that support PCID.
 *
 * The physical frames backing page tables themselves are obtained from
 * the `PhysicalManager`, so `PageMap` sits directly above the PMM and
 * directly below any higher‑level virtual memory policies.
 */
class PageMap {
   public:
    /**
     * @brief Initialize global paging environment.
     *
     * Sets CR0/CR4/EFER bits, detects features like NX/PCID, and
     * programs the PAT. Must be called before creating page maps.
     */
    static void global_init();

    /**
     * @brief Create a new page map with a fresh root.
     *
     * For the first call, this builds the kernel map. For subsequent
     * calls, the upper half (kernel space) is cloned from the kernel
     * map so all processes share kernel mappings.
     */
    static void create_new(PageMap* map);

    bool map(uintptr_t virt_addr, uintptr_t phys_addr, uint8_t flags, CacheType cache,
             PageSize size, uint8_t pkey = 0, bool do_flush = true);

    bool map(uintptr_t virt_addr, uint8_t flags, CacheType cache, PageSize size,
             bool do_flush = true);
    void map_range(uintptr_t virt_start, uintptr_t phys_start, size_t length, uint8_t flags,
                   CacheType cache);

    void unmap(uintptr_t virt_addr, uint16_t owner_pcid = 0, bool free_phys = false);
    uintptr_t translate(uintptr_t virt_addr);

    void load(uint16_t pcid = 0);

    uintptr_t get_root_phys() const {
        return this->phys_root_addr;
    }

    std::pair<uint8_t, CacheType> get_flags(uintptr_t virt_addr, PageSize size = PageSize::Size4K);
    uint8_t get_protection_key(uintptr_t virt_addr, PageSize size = PageSize::Size4K);

    static PageMap* get_kernel_map();

   private:
    uintptr_t* get_pte(uintptr_t virt_addr, int target_level, bool allocate);
    bool is_active() const;

    /// Physical address of the root page-table (CR3 value for this map).
    uintptr_t phys_root_addr;
    bool is_dirty = false;
};

}  // namespace kernel::memory