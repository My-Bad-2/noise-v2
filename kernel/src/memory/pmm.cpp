/**
 * @file pmm.cpp
 * @brief Implementation of the physical memory manager.
 *
 * Uses a bitmap to track page allocation state and a small stack-based
 * cache to accelerate single-page allocations and frees.
 */

#include "memory/pmm.hpp"
#include "boot/boot.h"
#include "libs/log.hpp"
#include "memory/memory.hpp"
#include "libs/spinlock.hpp"
#include "libs/math.hpp"
#include <string.h>

// Don't tell me what affects performance and what doesn't
// NOLINTBEGIN(performance-no-int-to-ptr)
namespace kernel::memory {
namespace {

// Global PMM state (bitmap, stack cache, statistics, lock).
struct {
    uint_least64_t* bitmap = nullptr;  // Allocation bitmap.
    size_t total_pages     = 0;        // Total number of managed pages.
    size_t bitmap_entries  = 0;        // Number of 64-bit entries in the bitmap.
    size_t free_idx_hint   = 0;        // Hint for next free-page search.
    size_t used_pages      = 0;        // Number of currently used pages.

    // Stack cache for quick single-page allocations.
    uintptr_t* free_stack = nullptr;
    size_t stack_top      = 0;
    size_t stack_capacity = 0;

    SpinLock lock;  // Protects PMM state.
} pmm_state;

// Number of bits in one bitmap entry.
constexpr int bit_count = sizeof(uint_least64_t) * 8;

}  // namespace

void PhysicalManager::set_bit(size_t idx) {
    size_t byte = idx / bit_count;
    size_t bit  = idx % bit_count;

    pmm_state.bitmap[byte] |= (1ULL << bit);
}

void PhysicalManager::clear_bit(size_t idx) {
    size_t byte = idx / bit_count;
    size_t bit  = idx % bit_count;

    pmm_state.bitmap[byte] &= ~(1ULL << bit);
}

bool PhysicalManager::test_bit(size_t idx) {
    size_t byte = idx / bit_count;
    size_t bit  = idx % bit_count;

    return pmm_state.bitmap[byte] & (1ULL << bit);
}

bool PhysicalManager::cache_push(void* addr) {
    // Push a freed single page into the stack cache if space is available.
    if (pmm_state.stack_top < pmm_state.stack_capacity) {
        pmm_state.free_stack[pmm_state.stack_top++] = reinterpret_cast<uintptr_t>(addr);
        return true;
    }

    return false;
}

void* PhysicalManager::cache_pop() {
    // Pop a page from the stack cache if any are available.
    if (pmm_state.stack_top > 0) {
        uintptr_t addr = pmm_state.free_stack[--pmm_state.stack_top];

        return reinterpret_cast<void*>(addr);
    }

    return nullptr;
}

void PhysicalManager::flush_cache_to_bitmap() {
    // Empty the stack back into the bitmap.
    while (pmm_state.stack_top > 0) {
        uintptr_t addr = pmm_state.free_stack[--pmm_state.stack_top];
        free_to_bitmap(addr / PMM_PAGE_SIZE, 1);
    }
}

void* PhysicalManager::alloc_from_bitmap(size_t count) {
    // Core bitmap-based allocation path (single or multi-page).
    if (count == 1) {
        // Fast path for single-page allocations: use free_idx_hint.
        size_t search_start = pmm_state.free_idx_hint / bit_count;

        for (size_t i = search_start; i < pmm_state.bitmap_entries; ++i) {
            uint_least64_t entry = pmm_state.bitmap[i];

            if (entry != static_cast<uint_least64_t>(-1)) {
                int bit_offset = __builtin_ctzll(~entry);
                size_t idx     = (i * bit_count) + static_cast<size_t>(bit_offset);

                if (idx < pmm_state.total_pages) {
                    set_bit(idx);
                    pmm_state.used_pages++;
                    pmm_state.free_idx_hint = idx + 1;

                    return reinterpret_cast<void*>(idx * PMM_PAGE_SIZE);
                }
            }
        }
    } else {
        // Multi-page allocation: search for a contiguous free run of `count` pages.
        auto try_allocate_range = [&](size_t start, size_t end) -> void* {
            size_t consecutive = 0;

            for (size_t i = start; i < end; ++i) {
                // Skip full 64-page words if we have no partial match.
                if ((consecutive == 0) && ((i % bit_count) == 0)) {
                    if (((i / 64) < pmm_state.bitmap_entries) &&
                        (pmm_state.bitmap[i / bit_count] == static_cast<uint_least64_t>(-1))) {
                        i += 63;
                        continue;
                    }
                }

                if (!test_bit(i)) {
                    consecutive++;

                    if (consecutive == count) {
                        // Found a block of `count` consecutive free pages.
                        size_t block_start = i - count + 1;

                        for (size_t j = 0; j < count; ++j) {
                            set_bit(block_start + j);
                        }

                        pmm_state.used_pages += count;
                        pmm_state.free_idx_hint = block_start + count;

                        return reinterpret_cast<void*>(block_start * PMM_PAGE_SIZE);
                    }
                } else {
                    consecutive = 0;
                }
            }

            return nullptr;
        };

        // Pass 1: Hint -> End.
        void* res = try_allocate_range(pmm_state.free_idx_hint, pmm_state.total_pages);

        if (res != nullptr) {
            return res;
        }

        // Pass 2: 0 -> Hint (wrap around).
        if (pmm_state.free_idx_hint > 0) {
            res = try_allocate_range(0, pmm_state.free_idx_hint);

            if (res != nullptr) {
                return res;
            }
        }
    }

    return nullptr;
}

void* PhysicalManager::alloc(size_t count) {
    if (count == 0) {
        return nullptr;
    }

    LockGuard guard(pmm_state.lock);

    if (count == 1) {
        // Try fast-path cache first for single-page allocations.
        void* cached = cache_pop();

        if (cached != nullptr) {
            pmm_state.used_pages++;
            LOG_DEBUG("PMM alloc (cached) page=%p used=%zu", cached, pmm_state.used_pages);
            return cached;
        }
    }

    void* addr = alloc_from_bitmap(count);
    if (addr != nullptr) {
        LOG_DEBUG("PMM alloc count=%zu addr=%p used_pages=%zu", count, addr, pmm_state.used_pages);
    } else {
        LOG_WARN("PMM alloc failed count=%zu", count);
    }

    return addr;
}

void* PhysicalManager::alloc_aligned(size_t count, size_t alignment) {
    if ((count == 0) || (alignment == 0) || (alignment % PMM_PAGE_SIZE != 0)) {
        LOG_WARN("PMM alloc_aligned invalid params count=%zu align=0x%zx", count, alignment);
        return nullptr;
    }

    size_t pages_per_alignment = alignment / PMM_PAGE_SIZE;

    LockGuard guard(pmm_state.lock);

    // Two-pass search.
    auto try_alloc = [&](size_t start, size_t end) -> void* {
        size_t curr = align_up(start, pages_per_alignment);

        while (curr < end) {
            // Ensure we don't overrun physical memory or the search range.
            if (curr + count > pmm_state.total_pages) {
                break;
            }

            // Inner scan: Check if `count` pages starting at `curr` are free.
            bool fit = true;
            for (size_t j = 0; j < count; ++j) {
                if (test_bit(curr + j)) {
                    fit = false;

                    // Found an allocated page at (curr + j); skip ahead to next alignment.
                    curr = align_up(curr + j + 1, pages_per_alignment);
                    break;
                }
            }

            if (fit) {
                // Mark pages as used.
                for (size_t j = 0; j < count; ++j) {
                    set_bit(curr + j);
                }

                pmm_state.used_pages += count;
                pmm_state.free_idx_hint = curr + count;

                void* addr = reinterpret_cast<void*>(curr * PMM_PAGE_SIZE);
                LOG_DEBUG("PMM alloc_aligned count=%zu align=0x%zx addr=%p used_pages=%zu",
                          count, alignment, addr, pmm_state.used_pages);
                return addr;
            }
        }

        return nullptr;
    };

    // Pass 1: Hint -> End.
    void* res = try_alloc(pmm_state.free_idx_hint, pmm_state.total_pages);

    if (res != nullptr) {
        return res;
    }

    // Pass 2: 0 -> Hint (wrap around).
    if (pmm_state.free_idx_hint > 0) {
        res = try_alloc(0, pmm_state.free_idx_hint);

        if (res != nullptr) {
            return res;
        }
    }

    LOG_WARN("PMM alloc_aligned failed count=%zu align=0x%zx", count, alignment);
    return nullptr;
}

void* PhysicalManager::alloc_clear(size_t count) {
    void* ret = alloc(count);

    if (ret != nullptr) {
        // Map into higher-half and clear contents.
        uintptr_t virt = to_higher_half(reinterpret_cast<uintptr_t>(ret));
        memset(reinterpret_cast<void*>(virt), 0, count * PMM_PAGE_SIZE);
        LOG_DEBUG("PMM alloc_clear count=%zu addr=%p", count, ret);
    }

    return ret;
}

// NOLINTNEXTLINE
void PhysicalManager::free_to_bitmap(size_t page_idx, size_t count) {
    // Free a range of pages in the bitmap.
    for (size_t i = 0; i < count; ++i) {
        size_t idx = page_idx + i;

        if (idx < pmm_state.total_pages && test_bit(idx)) {
            clear_bit(idx);
            pmm_state.used_pages--;
        }
    }

    if (page_idx < pmm_state.free_idx_hint) {
        pmm_state.free_idx_hint = page_idx;
    }
}

void PhysicalManager::free(void* ptr, size_t count) {
    if (ptr == nullptr) {
        return;
    }

    LockGuard guard(pmm_state.lock);

    if (count == 1) {
        // Attempt to push into the stack cache.
        if (cache_push(ptr)) {
            pmm_state.used_pages--;
            LOG_DEBUG("PMM free (cached) page=%p used=%zu", ptr, pmm_state.used_pages);
            return;
        }

        // Cache is full. Flush half the stack to bitmap to make space.
        size_t pages_to_flush = pmm_state.stack_capacity / 2;
        for (size_t i = 0; i < pages_to_flush; ++i) {
            void* flushed = cache_pop();

            if (flushed != nullptr) {
                // Pages in cache are currently "free" in stats.
                // free_to_bitmap() decrements used_pages again,
                // which would result in double-count.
                pmm_state.used_pages++;
                free_to_bitmap(reinterpret_cast<uintptr_t>(flushed) / PMM_PAGE_SIZE, 1);
            }
        }

        // Push the new page onto stack.
        cache_push(ptr);
        pmm_state.used_pages--;
        LOG_DEBUG("PMM free (after flush) page=%p used=%zu", ptr, pmm_state.used_pages);
        return;
    }

    // Multi-page free: go directly to bitmap.
    free_to_bitmap(reinterpret_cast<uintptr_t>(ptr) / PMM_PAGE_SIZE, count);
    LOG_DEBUG("PMM free range addr=%p count=%zu used_pages=%zu", ptr, count, pmm_state.used_pages);
}

void PhysicalManager::reclaim_type(size_t memmap_type) {
    // Reclaim all regions of a specific Limine memmap type.
    for (size_t i = 0; i < memmap_request.response->entry_count; ++i) {
        limine_memmap_entry* entry = memmap_request.response->entries[i];

        if (entry->type == memmap_type) {
            uintptr_t base_aligned = align_up(entry->base, PMM_PAGE_SIZE);
            uintptr_t end_aligned  = align_down(entry->base + entry->length, PMM_PAGE_SIZE);

            if (end_aligned > base_aligned) {
                size_t pages = (end_aligned - base_aligned) / PMM_PAGE_SIZE;
                free(reinterpret_cast<void*>(base_aligned), pages);
                LOG_INFO("PMM reclaimed type=%zu base=0x%lx pages=%zu",
                         memmap_type, base_aligned, pages);
            }
        }
    }
}

PMMStats PhysicalManager::get_stats() {
    LockGuard guard(pmm_state.lock);

    PMMStats stats     = {};
    stats.total_memory = pmm_state.total_pages * PMM_PAGE_SIZE;
    stats.used_memory  = pmm_state.used_pages * PMM_PAGE_SIZE;
    stats.free_memory  = (pmm_state.total_pages - pmm_state.used_pages) * PMM_PAGE_SIZE;

    return stats;
}

void PhysicalManager::init() {
    // Validate Limine memory map response.
    if ((memmap_request.response == nullptr) || memmap_request.response->entries == nullptr) {
        PANIC("Error in Limine Memory Map");
    }

    limine_memmap_entry** memmaps = memmap_request.response->entries;
    size_t memmap_count           = memmap_request.response->entry_count;

    // Ensure lock is in a known unlocked state.
    pmm_state.lock.unlock();
    uintptr_t highest_addr = 0;

    // Find highest address among usable/reclaimable types.
    for (size_t i = 0; i < memmap_count; ++i) {
        limine_memmap_entry* entry = memmaps[i];

        bool is_candidate = entry->type == LIMINE_MEMMAP_USABLE ||
                            entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
                            entry->type == LIMINE_MEMMAP_EXECUTABLE_AND_MODULES ||
                            entry->type == LIMINE_MEMMAP_ACPI_RECLAIMABLE;

        if (is_candidate) {
            uintptr_t end = entry->base + entry->length;

            if (end > highest_addr) {
                highest_addr = end;
            }
        }
    }

    pmm_state.total_pages = div_roundup(highest_addr, PMM_PAGE_SIZE);

    size_t bitmap_bytes      = align_up(div_roundup(pmm_state.total_pages, 8u), 8u);
    pmm_state.bitmap_entries = bitmap_bytes / 8;

    // Stack cache size (for stack array).
    size_t stack_bytes          = CACHE_SIZE * sizeof(uintptr_t);
    size_t total_metadata_bytes = bitmap_bytes + stack_bytes;

    // Find suitable hole for metadata.
    limine_memmap_entry* best_candidate = nullptr;

    for (size_t i = 0; i < memmap_count; ++i) {
        limine_memmap_entry* entry = memmaps[i];

        // Reject 0x0 base.
        if (entry->base == 0) {
            continue;
        }

        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= total_metadata_bytes) {
            if ((best_candidate == nullptr) || (entry->base > best_candidate->base)) {
                best_candidate = entry;
            }
        }
    }

    if (best_candidate == nullptr) {
        // If no high memory found, check for any suitable memory.
        for (size_t i = 0; i < memmap_count; ++i) {
            limine_memmap_entry* entry = memmaps[i];

            if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= total_metadata_bytes) {
                best_candidate = entry;
                break;
            }
        }
    }

    if (best_candidate == nullptr) {
        PANIC("No suitable memory hole found for metadata of size 0x%lx", total_metadata_bytes);
    }

    uintptr_t meta_base = best_candidate->base;
    if (meta_base == 0) {
        meta_base += PMM_PAGE_SIZE;
        best_candidate->length -= PMM_PAGE_SIZE;

        if (best_candidate->length < total_metadata_bytes) {
            PANIC("No suitable memory hole found for metadata of size 0x%lx", total_metadata_bytes);
        }
    }

    // Reserve metadata region.
    void* metadata_phys = reinterpret_cast<void*>(meta_base);
    best_candidate->base += total_metadata_bytes;
    best_candidate->length -= total_metadata_bytes;

    uintptr_t metadata_virt_addr = to_higher_half(reinterpret_cast<uintptr_t>(metadata_phys));

    // Bitmap starts at metadata base.
    pmm_state.bitmap = reinterpret_cast<uintptr_t*>(metadata_virt_addr);

    // Stack cache follows bitmap.
    pmm_state.free_stack = reinterpret_cast<uintptr_t*>(metadata_virt_addr + bitmap_bytes);

    pmm_state.stack_capacity = CACHE_SIZE;
    pmm_state.stack_top      = 0;

    // Initially mark all pages as used.
    memset(pmm_state.bitmap, 0xFF, bitmap_bytes);
    pmm_state.used_pages = pmm_state.total_pages;

    // Populate free memory from Limine map.
    for (size_t i = 0; i < memmap_count; ++i) {
        limine_memmap_entry* entry = memmaps[i];

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            size_t pages = entry->length / PMM_PAGE_SIZE;
            free(reinterpret_cast<void*>(entry->base), pages);
        }
    }

    LOG_INFO("PMM initialized: total_pages=%zu (~%zu MiB)", pmm_state.total_pages,
             (pmm_state.total_pages * PMM_PAGE_SIZE) >> 20);
}

}  // namespace kernel::memory

// NOLINTEND(performance-no-int-to-ptr)