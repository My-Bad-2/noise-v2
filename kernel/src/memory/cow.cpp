#include "libs/log.hpp"
#include "memory/paging.hpp"
#include "memory/cow.hpp"
#include "memory/pmm.hpp"
#include <string.h>

namespace kernel::memory {
uintptr_t CowManager::zero_page_phys = 0;

void CowManager::init() {
    zero_page_phys = reinterpret_cast<uintptr_t>(PhysicalManager::alloc());

    if (zero_page_phys == 0) {
        PANIC("Failed to allocate CoW Zero Page");
    }

    // Map zero page into higher half once and clear it.
    // All CoW mappings will refer to this same backing frame.
    void* virt = to_higher_half(reinterpret_cast<void*>(zero_page_phys));
    memset(virt, 0, PAGE_SIZE_4K);

    LOG_INFO("CoW: zero page initialized at phys=0x%lx", zero_page_phys);
}

uintptr_t CowManager::get_zero_page_phys() {
    return zero_page_phys;
}

bool CowManager::is_zero_page(uintptr_t virt_addr, PageMap* map) {
    uintptr_t phys = map->translate(virt_addr);
    // Compare the frame base; ignore lower offset bits.
    return (phys & ~page_mask) == zero_page_phys;
}

bool CowManager::handle_fault(uintptr_t virt_addr, PageMap* map) {
    if (!is_zero_page(virt_addr, map)) {
        // Not our problem: caller should treat this as a "real" fault.
        LOG_DEBUG("CoW: fault at 0x%lx is not zero-page backed", virt_addr);
        return false;
    }

    // Only 4 KiB pages are supported in CoW.
    uintptr_t new_phys = reinterpret_cast<uintptr_t>(PhysicalManager::alloc());
    if (new_phys == 0) {
        PANIC("Out of Memory during CoW fault!");
    }

    // Map new physical frame in higher half and copy contents.
    // Currently we know the old page is all-zero, so we just memset.
    void* new_virt = reinterpret_cast<void*>(to_higher_half(new_phys));
    memset(new_virt, 0, PAGE_SIZE_4K);

    // Rebuild PTE flags from the existing mapping:
    //  - Recover generic flags + cache policy.
    //  - Preserve protection key.
    //  - Turn the page into a regular writable mapping (clear Lazy, set Write).
    auto [flags, cache] = map->get_flags(virt_addr);
    uint8_t pkey        = map->get_protection_key(virt_addr);
    flags |= Write;
    flags &= ~Lazy;

    LOG_DEBUG("CoW: resolving fault at 0x%lx -> new_phys=0x%lx flags=0x%x pkey=%u", virt_addr,
              new_phys, flags, pkey);

    // Remap the faulting VA to the new private frame.
    map->map(virt_addr, new_phys, flags, cache, PageSize::Size4K, pkey);

    return true;
}
}  // namespace kernel::memory