/**
 * @file cow.cpp
 * @brief Copy-on-Write (CoW) page management for anonymous kernel pages.
 *
 * The CoW manager implements a simple optimization for lazily allocated
 * memory: instead of backing every newly reserved virtual page with a
 * unique physical frame, many pages can initially share a single
 * read-only "zero page". When a write fault occurs, the manager:
 *
 *  - Detects that the faulting mapping points at the global zero page.
 *  - Allocates a private 4 KiB frame.
 *  - Remaps the faulting virtual address to this new frame with write
 *    permission and without the `Lazy` flag.
 *
 * Design motivations:
 *  - **Avoid eager zeroing**: most freshly allocated pages are touched
 *    sparsely or not at all. Sharing a pre-zeroed page defers work
 *    until first write.
 *  - **Bounded complexity**: only 4 KiB pages participate in CoW. This
 *    avoids dealing with partial splits of huge pages and keeps the
 *    implementation tightly scoped.
 */

#include "libs/log.hpp"
#include "memory/memory.hpp"
#include "memory/paging.hpp"
#include "memory/vmm.hpp"
#include "memory/pmm.hpp"
#include <string.h>

namespace kernel::memory {
uintptr_t CowManager::zero_page_phys = 0;

/**
 * @brief Initialize the global CoW zero page.
 *
 * Allocates a single 4 KiB physical frame, clears it to all-zero and
 * keeps its physical address. All lazily allocated pages start out
 * mapping to this shared frame.
 */
void CowManager::init() {
    zero_page_phys = reinterpret_cast<uintptr_t>(PhysicalManager::alloc());

    if (zero_page_phys == 0) {
        PANIC("Failed to allocate CoW Zero Page");
    }

    // Map zero page into higher half once and clear it.
    // All CoW mappings will refer to this same backing frame.
    void* virt = to_higher_half(reinterpret_cast<void*>(zero_page_phys));  // NOLINT
    memset(virt, 0, PAGE_SIZE_4K);

    LOG_INFO("CoW: zero page initialized at phys=0x%lx", zero_page_phys);
}

uintptr_t CowManager::get_zero_page_phys() {
    return zero_page_phys;
}

/**
 * @brief Check whether a virtual address maps to the global zero page.
 *
 * This allows the fault handler to distinguish "real" faults from
 * CoW-first-write events without inspecting higher-level state.
 */
bool CowManager::is_zero_page(uintptr_t virt_addr, PageMap* map) {
    uintptr_t phys = map->translate(virt_addr);
    // Compare the frame base; ignore lower offset bits.
    return (phys & ~page_mask) == zero_page_phys;
}

/**
 * @brief Handle a potential CoW page fault.
 *
 * Semantics:
 *  - If the faulting address is not backed by the global zero page,
 *    the fault is unrelated to CoW and the function returns false.
 *  - If it *is* the zero page, a private writable 4 KiB frame is
 *    allocated and substituted, and the function returns true.
 *
 * Design notes:
 *  - CoW is deliberately limited to 4 KiB pages to avoid splitting
 *    large (2 MiB/1 GiB) mappings on demand.
 *  - The `Lazy` flag is cleared so the page is handled as a regular
 *    writable page going forward.
 */
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
    void* new_virt = reinterpret_cast<void*>(to_higher_half(new_phys));  // NOLINTNEXTLINE
    memset(new_virt, 0, PAGE_SIZE_4K);

    // Rebuild PTE flags from the existing mapping:
    //  - Recover generic flags + cache policy.
    //  - Preserve protection key.
    //  - Turn the page into a regular writable mapping (clear Lazy, set Write).
    auto [flags, cache] = map->get_flags(virt_addr);
    uint8_t pkey        = map->get_protection_key(virt_addr);
    flags |= Write;
    flags &= ~Lazy;

    LOG_DEBUG("CoW: resolving fault at 0x%lx -> new_phys=0x%lx flags=0x%x pkey=%u",
              virt_addr, new_phys, flags, pkey);

    // Remap the faulting VA to the new private frame.
    map->map(virt_addr, new_phys, flags, cache, PageSize::Size4K, pkey);

    return true;
}
}  // namespace kernel::memory