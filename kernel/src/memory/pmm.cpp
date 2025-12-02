/**
 * @file pmm.cpp
 * @brief Implementation of the physical memory manager.
 *
 * Uses a bitmap to track page allocation state and a small stack-based
 * cache to accelerate single-page allocations and frees.
 *
 * A second-level *summary bitmap* marks which 64-page blocks are fully
 * used, allowing the allocator to skip large contiguous allocated regions
 * quickly when searching for free pages.
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

// Global PMM state (bitmap, summary bitmap, stack cache, statistics, lock).
struct {
    uint_least64_t* bitmap         = nullptr;  // Allocation bitmap (1 bit per page).
    uint_least64_t* summary_bitmap = nullptr;  // Summary bitmap (1 bit per 64 pages).

    size_t total_pages     = 0;  // Total number of managed pages.
    size_t summary_entries = 0;  // Number of 64-bit entries in the summary bitmap.
    size_t bitmap_entries  = 0;  // Number of 64-bit entries in the bitmap.
    size_t free_idx_hint   = 0;  // Hint for next free-page search.
    size_t used_pages      = 0;  // Number of currently used pages.

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

    // If this 64-page block becomes fully occupied (all bits set), mark it
    // as full in the summary bitmap so future searches can skip it.
    if (pmm_state.bitmap[byte] == static_cast<size_t>(-1)) {
        size_t summary_byte = byte / bit_count;
        pmm_state.summary_bitmap[summary_byte] |= (1ULL << (byte % bit_count));
    }
}

void PhysicalManager::clear_bit(size_t idx) {
    size_t byte         = idx / bit_count;
    size_t bit          = idx % bit_count;
    size_t summary_byte = byte / bit_count;

    pmm_state.bitmap[byte] &= ~(1ULL << bit);

    // This 64-page block is now definitely not full; mark it as such in
    // the summary bitmap so it becomes a candidate during future scans.
    pmm_state.summary_bitmap[summary_byte] &= ~(1ULL << (byte % bit_count));
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
        // Fast path for single-page allocations: use free_idx_hint and the
        // summary bitmap to skip fully used 64-page groups quickly.
        size_t search_start         = pmm_state.free_idx_hint / bit_count;
        size_t search_start_summary = search_start / bit_count;

        for (size_t s = search_start_summary; s < pmm_state.summary_entries; ++s) {
            // Each summary entry covers 64 bitmap entries = 4096 pages.
            uint_least64_t summary_entry = pmm_state.summary_bitmap[s];

            // 64 blocks (4096 pages) are totally full; skip them.
            if (summary_entry == static_cast<uint_least64_t>(-1)) {
                continue;
            }

            // Find a bitmap word (block of 64 pages) with at least one free page.
            int block_offset = __builtin_ctzll(~summary_entry);
            size_t map_idx   = (s * bit_count) + static_cast<size_t>(block_offset);

            // Now verify the actual bitmap word.
            if (map_idx < pmm_state.bitmap_entries) {
                uint_least64_t entry = pmm_state.bitmap[map_idx];

                if (entry != static_cast<uint_least64_t>(-1)) {
                    // Find a free bit inside this word.
                    int bit_offset = __builtin_ctzll(~entry);
                    size_t idx     = (map_idx * bit_count) + static_cast<size_t>(bit_offset);

                    if (idx < pmm_state.total_pages) {
                        set_bit(idx);
                        pmm_state.used_pages++;
                        pmm_state.free_idx_hint = idx + 1;

                        return reinterpret_cast<void*>(idx * PMM_PAGE_SIZE);
                    }
                }
            }
        }

        // Wrap around: if we started past 0, scan from 0 to the start hint.
        if (pmm_state.free_idx_hint > 0) {
            for (size_t s = 0; s < search_start_summary; ++s) {
                uint_least64_t summary_entry = pmm_state.summary_bitmap[s];

                // 64 blocks (4096 pages) are totally full; skip them.
                if (summary_entry == static_cast<uint_least64_t>(-1)) {
                    continue;
                }

                int block_offset = __builtin_ctzll(~summary_entry);
                size_t map_idx   = (s * bit_count) + static_cast<size_t>(block_offset);

                if (map_idx < pmm_state.bitmap_entries) {
                    uint_least64_t entry = pmm_state.bitmap[map_idx];

                    if (entry != static_cast<uint_least64_t>(-1)) {
                        int bit_offset = __builtin_ctzll(~entry);
                        size_t idx     = (map_idx * bit_count) + static_cast<size_t>(bit_offset);

                        if (idx < pmm_state.total_pages) {
                            set_bit(idx);
                            pmm_state.used_pages++;
                            pmm_state.free_idx_hint = idx + 1;

                            return reinterpret_cast<void*>(idx * PMM_PAGE_SIZE);
                        }
                    }
                }
            }
        }
    } else {
        // Multi-page allocation: search for a contiguous free run of `count` pages.
        auto try_allocate_range = [&](size_t start, size_t end) -> void* {
            size_t consecutive = 0;

            for (size_t i = start; i < end; ++i) {
                if ((consecutive == 0) && ((i % bit_count) == 0)) {
                    size_t map_idx = i / bit_count;

                    // If this bitmap word is full, consider skipping larger chunks.
                    if ((map_idx < pmm_state.bitmap_entries) &&
                        (pmm_state.bitmap[map_idx] == static_cast<size_t>(-1))) {
                        // Check if the entire "superpage" (64 words = 4096 pages)
                        // is full using the summary bitmap.
                        if ((map_idx % bit_count) == 0) {
                            size_t summary_idx = map_idx / bit_count;

                            if ((summary_idx < pmm_state.summary_entries) &&
                                (pmm_state.summary_bitmap[summary_idx] ==
                                 static_cast<size_t>(-1))) {
                                // Massive skip: 4096 pages.
                                i += (bit_count * bit_count) - 1;
                                continue;
                            }
                        }

                        // Otherwise, skip this 64-page word.
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
                LOG_DEBUG("PMM alloc_aligned count=%zu align=0x%zx addr=%p used_pages=%zu", count,
                          alignment, addr, pmm_state.used_pages);
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
                LOG_INFO("PMM reclaimed type=%zu base=0x%lx pages=%zu", memmap_type, base_aligned,
                         pages);
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
    LOG_INFO("PMM: highest_addr=0x%lx total_pages=%zu", highest_addr, pmm_state.total_pages);

    size_t bitmap_bytes      = align_up(div_roundup(pmm_state.total_pages, 8u), 8u);
    pmm_state.bitmap_entries = bitmap_bytes / 8;

    // Calculate summary bitmap size: one bit per bitmap entry.
    size_t summary_bits       = pmm_state.bitmap_entries;
    size_t summary_bytes      = align_up(div_roundup(summary_bits, 8u), 8u);
    pmm_state.summary_entries = summary_bytes / 8;

    // Stack cache size (for stack array).
    size_t stack_bytes          = CACHE_SIZE * sizeof(uintptr_t);
    size_t total_metadata_bytes = bitmap_bytes + summary_bytes + stack_bytes;

    LOG_DEBUG("PMM: bitmap_bytes=%zu summary_bytes=%zu stack_bytes=%zu metadata_total=%zu",
              bitmap_bytes, summary_bytes, stack_bytes, total_metadata_bytes);

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
    LOG_INFO("PMM: using metadata base=0x%lx in region (base=0x%lx len=0x%lx)", meta_base,
             best_candidate->base, best_candidate->length);

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

    // Place summary bitmap after main bitmap.
    pmm_state.summary_bitmap = reinterpret_cast<uintptr_t*>(metadata_virt_addr + bitmap_bytes);

    // Stack cache follows the bitmaps.
    pmm_state.free_stack =
        reinterpret_cast<uintptr_t*>(metadata_virt_addr + bitmap_bytes + summary_bytes);

    pmm_state.stack_capacity = CACHE_SIZE;
    pmm_state.stack_top      = 0;

    LOG_DEBUG("PMM: bitmap@%lp (%zu entries), summary@%p (%zu entries), stack@%p", pmm_state.bitmap,
              pmm_state.bitmap_entries, pmm_state.summary_bitmap, pmm_state.summary_entries,
              pmm_state.free_stack);

    // Initially mark all pages as used.
    memset(pmm_state.bitmap, 0xFF, bitmap_bytes);
    memset(pmm_state.summary_bitmap, 0xFF, summary_bytes);
    pmm_state.used_pages = pmm_state.total_pages;

    // Populate free memory from Limine map.
    size_t reclaimed_pages = 0;
    for (size_t i = 0; i < memmap_count; ++i) {
        limine_memmap_entry* entry = memmaps[i];

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            size_t pages = entry->length / PMM_PAGE_SIZE;
            reclaimed_pages += pages;
            free(reinterpret_cast<void*>(entry->base), pages);
        }
    }

    PMMStats stats = get_stats();
    LOG_INFO("PMM initialized: total_pages=%zu (~%zu MiB), reclaimed_pages=%zu, free=%zu MiB",
             pmm_state.total_pages, (pmm_state.total_pages * PMM_PAGE_SIZE) >> 20, reclaimed_pages,
             stats.free_memory >> 20);
}

}  // namespace kernel::memory

// NOLINTEND(performance-no-int-to-ptr)