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

class PageMap {
   public:
    static void global_init();
    static void create_new(PageMap* map);

    bool map(uintptr_t virt_addr, uintptr_t phys_addr, uint8_t flags, CacheType cache,
             PageSize size, uint8_t pkey = 0, bool do_flush = true);

    bool map(uintptr_t virt_addr, uint8_t flags, CacheType cache, PageSize size,
             bool do_flush = true);
    void map_range(uintptr_t virt_start, uintptr_t phys_start, size_t length, uint8_t flags,
                   CacheType cache);

    void unmap(uintptr_t virt_addr, uint16_t owner_pcid = 0, bool free_phys = false);
    uintptr_t translate(uintptr_t virt_addr);

    void load(uint16_t pcid = 0, bool flush = true);

    uintptr_t get_root_phys() const {
        return this->phys_root_addr;
    }

    std::pair<uint8_t, CacheType> get_flags(uintptr_t virt_addr, PageSize size = PageSize::Size4K);
    uint8_t get_protection_key(uintptr_t virt_addr, PageSize size = PageSize::Size4K);

    static PageMap* get_kernel_map();

   private:
    static bool is_table_empty(uintptr_t* table);

    uintptr_t* get_pte(uintptr_t virt_addr, int target_level, bool allocate);
    bool is_active() const;

    /// Physical address of the root page-table.
    uintptr_t phys_root_addr;
};
}  // namespace kernel::memory