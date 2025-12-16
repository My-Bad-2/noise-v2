#include "memory/pmm.hpp"
#include "boot/boot.h"
#include "cpu/cpu.hpp"
#include "hal/cpu.hpp"
#include "libs/log.hpp"
#include "memory/memory.hpp"
#include "libs/spinlock.hpp"
#include "libs/math.hpp"
#include <stdlib.h>
#include <string.h>
#include <cstdint>

namespace kernel::memory {
struct alignas(CACHE_LINE_SIZE) PhysicalManager::PerCPUCache {
    uintptr_t* stack;  // Pointer to the stack memroy for this CPU
    size_t count;      // Number of pages in stack
    size_t capacity;   // Max Capacity (CACHE_SIZE)
};

namespace {
// Global PMM state (bitmap, summary bitmap, stack cache, statistics, lock).
struct {
    uint_least64_t* bitmap         = nullptr;  // Allocation bitmap (1 bit per page).
    uint_least64_t* summary_bitmap = nullptr;  // Summary bitmap (1 bit per 64 pages).

    size_t total_pages           = 0;  // Total number of managed pages.
    size_t summary_entries       = 0;  // Number of 64-bit entries in the summary bitmap.
    size_t bitmap_entries        = 0;  // Number of 64-bit entries in the bitmap.
    size_t used_pages            = 0;  // Number of currently used pages.
    size_t low_mem_threshold_idx = 0;  // page at which phys_addr >= 4GB
    size_t free_idx_hint         = 0;  // Hint for next free-page search (small allocations)
    size_t aligned_index_hint    = 0;  // Hint for next free-page search (huge allocations)

    PhysicalManager::PerCPUCache* cpus = nullptr;
    size_t num_cpus                    = 1;

    IrqLock lock;  // Protects PMM state.
    InterruptLock interrupt_lock;
} pmm_state;

// Number of bits in one bitmap entry.
constexpr uint8_t bit_count = sizeof(uint_least64_t) * 8;
}  // namespace

void PhysicalManager::set_bit(size_t idx) {
    size_t byte = idx / bit_count;
    size_t bit  = idx % bit_count;

    pmm_state.bitmap[byte] |= (1ULL << bit);

    // When a 64-page block transitions to "completely full", we mark it
    // in the summary bitmap.
    if (pmm_state.bitmap[byte] == static_cast<size_t>(-1)) {
        size_t summary_byte = byte / bit_count;
        size_t summary_bit  = byte % bit_count;
        pmm_state.summary_bitmap[summary_byte] |= (1ULL << summary_bit);
    }
}

void PhysicalManager::clear_bit(size_t idx) {
    size_t byte         = idx / bit_count;
    size_t bit          = idx % bit_count;
    size_t summary_byte = byte / bit_count;
    size_t summary_bit  = byte % bit_count;

    pmm_state.bitmap[byte] &= ~(1ULL << bit);

    // Any cleared bit means this 64-page block is no longer "fully used".
    // We eagerly clear the summary bit so that future scans will see this
    // region as a candidate without having to probe the entire word first.
    pmm_state.summary_bitmap[summary_byte] &= ~(1ULL << summary_bit);
}

bool PhysicalManager::test_bit(size_t idx) {
    size_t byte = idx / bit_count;
    size_t bit  = idx % bit_count;

    return pmm_state.bitmap[byte] & (1ULL << bit);
}

void PhysicalManager::cache_refill(PerCPUCache& cache) {
    // Calculate number of page to transfer
    size_t need = BATCH_SIZE;

    if (cache.count + need > cache.capacity) {
        need = cache.capacity - cache.capacity;
    }

    if (need == 0) {
        return;
    }

    size_t collected = 0;

    auto scan_bitmap_range = [&](size_t start_idx, size_t end_idx) -> bool {
        size_t start = start_idx / bit_count;
        size_t end   = div_roundup(end_idx, bit_count);

        for (size_t s = start; s < end; ++s) {
            // Prefech the summary bitmap (~2 cache lines)
            __builtin_prefetch(&pmm_state.summary_bitmap[s + 2], 0, 1);

            if (pmm_state.summary_bitmap[s] == ~0ull) {
                continue;
            }

            uint_least64_t summary_val = pmm_state.summary_bitmap[s];
            uint_least64_t inv_summary = ~summary_val;

            while (inv_summary) {
                int block_bit   = __builtin_ctzll(inv_summary);
                size_t word_idx = (s * bit_count) + static_cast<size_t>(block_bit);

                if (word_idx >= end_idx) {
                    return false;
                }

                // Prefetch the main bitmap (~4 cache lines)
                __builtin_prefetch(&pmm_state.bitmap[word_idx + 4], 1, 1);

                uint_least64_t word_val = pmm_state.bitmap[word_idx];

                // If word is not full, extract pages
                if (word_val != ~0ull) {
                    if (word_val == 0 && (need - collected) >= 64) {
                        pmm_state.bitmap[word_idx] = ~0ull;  // Mark full
                        pmm_state.summary_bitmap[s] |= (1ul << block_bit);

                        uint_least64_t base_page = word_idx * bit_count;

                        for (size_t k = 0; k < bit_count; ++k) {
                            cache.stack[cache.count++] = (base_page + k) * PAGE_SIZE_4K;
                        }

                        collected += bit_count;
                    } else {
                        uint_least64_t inv_word = ~word_val;
                        bool word_modified      = false;

                        while (inv_word && (collected < need)) {
                            int page_bit = __builtin_ctzll(inv_word);
                            size_t page_idx =
                                (word_idx * bit_count) + static_cast<size_t>(page_bit);

                            if (page_idx >= pmm_state.total_pages) {
                                return true;
                            }

                            // Add to CPU cache
                            cache.stack[cache.count++] = page_idx * PAGE_SIZE_4K;
                            collected++;

                            // Mark as used
                            word_val |= (1ul << page_bit);
                            inv_word &= ~(1ul << page_bit);

                            word_modified = true;
                        }

                        if (word_modified) {
                            // Write back to global bitmap
                            pmm_state.bitmap[word_idx] = word_val;

                            // Update summary bitmap
                            if (word_val == ~0ull) {
                                pmm_state.summary_bitmap[s] |= (1ul << block_bit);
                            }
                        }
                    }

                    if (collected >= need) {
                        // Update hint for next allocation
                        pmm_state.free_idx_hint = word_idx * bit_count;
                        return true;
                    }
                }

                // Clear bit to move to next free word in this summary block
                inv_summary &= ~(1ul << block_bit);
            }
        }

        // Continue to next range
        return false;
    };

    size_t start_idx = pmm_state.free_idx_hint / bit_count;

    // Prioritize High mem (>4GB) if we actually have High mem (threshold < total)
    // and we are currently looking at low mem.
    if ((pmm_state.low_mem_threshold_idx < pmm_state.total_pages) &&
        (pmm_state.free_idx_hint < pmm_state.low_mem_threshold_idx)) {
        start_idx = pmm_state.low_mem_threshold_idx / bit_count;
    }

    // Scan from Hint -> End of Memory
    if (!scan_bitmap_range(start_idx, pmm_state.bitmap_entries) && (start_idx > 0)) {
        // Wrap around (Scan from 0 -> Hint)
        scan_bitmap_range(0, start_idx);
    }

    pmm_state.used_pages += collected;
}

void PhysicalManager::cache_flush(PerCPUCache& cache) {
    size_t target_count = cache.capacity / 2;

    if (cache.capacity <= target_count) {
        return;
    }

    size_t flush_count     = cache.capacity - target_count;
    uintptr_t* flush_start = &cache.stack[target_count];

    // Sort the addresses in order, so they hit consecutive bitmap words.
    if (flush_count > 1) {
        qsort(flush_start, flush_count, sizeof(uintptr_t), [](const void* a, const void* b) -> int {
            uintptr_t arg1 = *static_cast<const uintptr_t*>(a);
            uintptr_t arg2 = *static_cast<const uintptr_t*>(b);

            if (arg1 < arg2) {
                return -1;
            }

            if (arg1 > arg2) {
                return 1;
            }

            return 0;
        });
    }

    size_t last_word_idx         = ~0ull;
    uint_least64_t curr_word_val = 0;
    bool dirty                   = false;

    for (size_t i = 0; i < flush_count; ++i) {
        uintptr_t phys_addr = flush_start[i];
        size_t page_idx     = phys_addr / PAGE_SIZE_4K;

        if (page_idx >= pmm_state.total_pages) {
            continue;
        }

        size_t word_idx = page_idx / bit_count;
        size_t bit_idx  = page_idx % bit_count;

        uint_least64_t mask = (1ul << bit_idx);

        // If we moved to a new word, commit the previous one to the global state
        if (word_idx != last_word_idx) {
            if (dirty && (last_word_idx < pmm_state.bitmap_entries)) {
                const size_t last_word_byte = last_word_idx / bit_count;
                const size_t last_word_bit  = last_word_idx % bit_count;

                pmm_state.bitmap[last_word_idx] = curr_word_val;
                pmm_state.summary_bitmap[last_word_byte] &= ~(1ul << last_word_bit);
            }

            // Load new word
            last_word_idx = word_idx;
            curr_word_val = pmm_state.bitmap[word_idx];
            dirty         = true;
        }

        // Clear the bit in the local copy
        curr_word_val &= ~mask;
        pmm_state.used_pages--;

        if (page_idx < pmm_state.free_idx_hint) {
            pmm_state.free_idx_hint = page_idx;
        }

        if (page_idx < pmm_state.aligned_index_hint) {
            pmm_state.aligned_index_hint = page_idx;
        }
    }

    if (dirty && (last_word_idx < pmm_state.bitmap_entries)) {
        const size_t last_word_byte = last_word_idx / bit_count;
        const size_t last_word_bit  = last_word_idx % bit_count;

        pmm_state.bitmap[last_word_idx] = curr_word_val;
        pmm_state.summary_bitmap[last_word_byte] &= ~(1ul << last_word_bit);
    }

    cache.count = target_count;
}

void* PhysicalManager::alloc_from_bitmap(size_t count) {
    size_t start_idx = pmm_state.free_idx_hint;

    if ((pmm_state.low_mem_threshold_idx < pmm_state.total_pages) &&
        (start_idx < pmm_state.low_mem_threshold_idx)) {
        start_idx = pmm_state.low_mem_threshold_idx;
    }

    // Core bitmap-based allocation path (single or multi-page).
    if (count == 1) {
        // Single-page allocations prefer to find a "mostly free" region
        // using the summary bitmap, then drill down into the main bitmap.
        size_t search_start         = start_idx / bit_count;
        size_t search_start_summary = search_start / bit_count;

        for (size_t s = search_start_summary; s < pmm_state.summary_entries; ++s) {
            __builtin_prefetch(&pmm_state.summary_bitmap[s + 4], 0, 1);

            // Each summary entry covers 64 bitmap entries = 4096 pages.
            uint_least64_t summary_entry = pmm_state.summary_bitmap[s];

            // Summary == all ones => every tracked 64-page block here is full.
            // Skipping avoids touching obviously-saturated regions at all.
            if (summary_entry == static_cast<uint_least64_t>(-1)) {
                continue;
            }

            // Find a bitmap word (block of 64 pages) with at least one free page.
            int block_offset = __builtin_ctzll(~summary_entry);
            size_t map_idx   = (s * bit_count) + static_cast<size_t>(block_offset);

            // Now verify the actual bitmap word and pick the first free page in it.
            if (map_idx < pmm_state.bitmap_entries) {
                __builtin_prefetch(&pmm_state.bitmap[map_idx + 8], 0, 1);
                uint_least64_t entry = pmm_state.bitmap[map_idx];

                if (entry != static_cast<uint_least64_t>(-1)) {
                    int bit_offset = __builtin_ctzll(~entry);
                    size_t idx     = (map_idx * bit_count) + static_cast<size_t>(bit_offset);

                    if (idx < pmm_state.total_pages) {
                        set_bit(idx);
                        pmm_state.used_pages++;
                        pmm_state.free_idx_hint = idx + 1;

                        // LOG_DEBUG("PMM alloc_from_bitmap (summary path) page=%zu", idx);
                        return reinterpret_cast<void*>(idx * PAGE_SIZE_4K);
                    }
                }
            }
        }

        // Wrap around: if we started past 0, scan from 0 to the start hint.
        if (pmm_state.free_idx_hint > 0) {
            for (size_t s = 0; s < search_start_summary; ++s) {
                uint_least64_t summary_entry = pmm_state.summary_bitmap[s];

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

                            // LOG_DEBUG("PMM alloc_from_bitmap (wrap+summary) page=%zu", idx);
                            return reinterpret_cast<void*>(idx * PAGE_SIZE_4K);
                        }
                    }
                }
            }
        }
    }

    return alloc_aligned(count, PAGE_SIZE_4K);
}

void* PhysicalManager::alloc(size_t count) {
    if (count == 0) {
        return nullptr;
    }

    if ((count == 1) && pmm_state.cpus) {
        LockGuard guard(pmm_state.interrupt_lock);

        size_t core_id = 0;

        if (cpu::CPUCoreManager::initialized()) {
            core_id = cpu::CPUCoreManager::get_curr_cpu_id();
        }

        if (core_id < pmm_state.num_cpus) {
            PerCPUCache& cache = pmm_state.cpus[core_id];

            if (cache.count == 0) {
                LockGuard _(pmm_state.lock);
                cache_refill(cache);
            }

            if (cache.count > 0) {
                return reinterpret_cast<void*>(cache.stack[--cache.count]);
            }
        }
    }

    LockGuard guard(pmm_state.lock);

    void* addr = alloc_from_bitmap(count);
    if (addr != nullptr) {
        // LOG_DEBUG("PMM alloc (bitmap) count=%zu addr=%p used_pages=%zu", count, addr,
        //   pmm_state.used_pages);
    } else {
        LOG_WARN("PMM alloc failed count=%zu", count);
    }

    return addr;
}

void* PhysicalManager::alloc_aligned(size_t count, size_t alignment) {
    if ((count == 0) || (alignment == 0) || (alignment % PAGE_SIZE_4K != 0)) {
        LOG_WARN("PMM alloc_aligned invalid params count=%zu align=0x%zx", count, alignment);
        return nullptr;
    }

    size_t pages_per_alignment = alignment / PAGE_SIZE_4K;

    LockGuard guard(pmm_state.lock);

    size_t start_hint = pmm_state.free_idx_hint;

    // Prefer High Memory
    if ((pmm_state.low_mem_threshold_idx < pmm_state.total_pages) &&
        (start_hint < pmm_state.low_mem_threshold_idx)) {
        start_hint = pmm_state.low_mem_threshold_idx;
    }

    // Use the aligned hint if alignment is large
    if (alignment >= PAGE_SIZE_2M) {
        start_hint = pmm_state.aligned_index_hint;
    }

    // Two-pass search.
    auto try_alloc = [&](size_t start, size_t end) -> void* {
        size_t curr = align_up(start, pages_per_alignment);

        while (curr < end) {
            // Ensure we don't overrun physical memory or the search range.
            if (curr + count > pmm_state.total_pages) {
                break;
            }

            if ((curr % bit_count) == 0 && (count >= bit_count)) {
                size_t summary_idx = curr / bit_count / bit_count;

                if ((summary_idx < pmm_state.summary_entries) &&
                    (pmm_state.summary_bitmap[summary_idx] == ~0ul)) {
                    curr += (bit_count * bit_count);
                    curr = align_up(curr, pages_per_alignment);
                    continue;
                }
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
                if (alignment >= PAGE_SIZE_2M) {
                    pmm_state.aligned_index_hint = curr + count;
                } else {
                    pmm_state.free_idx_hint = curr + count;
                }

                void* addr = reinterpret_cast<void*>(curr * PAGE_SIZE_4K);
                // LOG_DEBUG("PMM alloc_aligned count=%zu align=0x%zx addr=%p used_pages=%zu",
                // count,
                //   alignment, addr, pmm_state.used_pages);
                return addr;
            }
        }

        return nullptr;
    };

    // Pass 1: Hint -> End.
    void* res = try_alloc(start_hint, pmm_state.total_pages);

    if (res) {
        return res;
    }

    // Pass 2: 0 -> Hint (wrap around).
    if (pmm_state.free_idx_hint > 0) {
        res = try_alloc(0, pmm_state.free_idx_hint);

        if (res) {
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
        memset(reinterpret_cast<void*>(virt), 0, count * PAGE_SIZE_4K);
        // LOG_DEBUG("PMM alloc_clear count=%zu addr=%p", count, ret);
    }

    return ret;
}

void* PhysicalManager::alloc_dma(size_t count, size_t alignment) {
    if ((count == 0) || (alignment == 0) || (alignment % PAGE_SIZE_4K != 0)) {
        LOG_WARN("PMM alloc_dma invalid params count=%zu align=0x%zx", count, alignment);
        return nullptr;
    }

    size_t pages_per_alignment = alignment / PAGE_SIZE_4K;

    LockGuard guard(pmm_state.lock);

    size_t limit = pmm_state.low_mem_threshold_idx;
    size_t curr  = 0;

    while (curr < limit) {
        if ((curr + count) > limit) {
            break;
        }

        if ((curr % bit_count) == 0 && (count >= bit_count)) {
            size_t summary_idx = curr / bit_count / bit_count;

            if ((summary_idx < pmm_state.summary_entries) &&
                (pmm_state.summary_bitmap[summary_idx] == ~0ul)) {
                curr += (bit_count * bit_count);
                curr = align_up(curr, pages_per_alignment);
                continue;
            }
        }

        bool fit = true;

        for (size_t i = 0; i < count; ++i) {
            if (test_bit(curr + i)) {
                fit  = false;
                curr = align_up(curr + i + 1, pages_per_alignment);
                break;
            }
        }

        if (fit) {
            for (size_t i = 0; i < count; ++i) {
                set_bit(curr + i);
            }

            pmm_state.used_pages += count;

            return reinterpret_cast<void*>(curr * PAGE_SIZE_4K);
        }
    }

    return nullptr;
}

void PhysicalManager::free_to_bitmap(size_t page_idx, size_t count) {
    size_t curr      = page_idx;
    size_t remaining = count;

    // Free individual bits until 64-page aligned
    while ((remaining > 0) && (curr % bit_count) != 0) {
        if ((curr < pmm_state.total_pages) && test_bit(curr)) {
            clear_bit(curr);
            pmm_state.used_pages--;
        }

        curr++;
        remaining--;
    }

    // Free 64 pages at a time using word writes
    while (remaining >= bit_count) {
        size_t word_idx = curr / bit_count;

        if (word_idx < pmm_state.bitmap_entries) {
            uint_least64_t old_val = pmm_state.bitmap[word_idx];

            if (old_val != 0) {
                const size_t byte = word_idx / bit_count;
                const size_t bit  = word_idx % bit_count;

                // Count how many bits are flipped from 1->0
                pmm_state.used_pages -= static_cast<size_t>(__builtin_popcountll(old_val));
                pmm_state.bitmap[word_idx] = 0;

                pmm_state.summary_bitmap[byte] &= ~(1ul << bit);
            }
        }

        curr += bit_count;
        remaining -= bit_count;
    }

    // Free remaining individual bits
    while (remaining > 0) {
        if ((curr < pmm_state.total_pages) && test_bit(curr)) {
            clear_bit(curr);
            pmm_state.used_pages--;
        }

        curr++;
        remaining--;
    }

    // Hint Update
    if ((page_idx < pmm_state.free_idx_hint) && (page_idx > pmm_state.low_mem_threshold_idx)) {
        pmm_state.free_idx_hint = page_idx;
    }
}

void PhysicalManager::free(void* ptr, size_t count) {
    if (ptr == nullptr) {
        return;
    }

    if ((count == 1) && (pmm_state.cpus)) {
        LockGuard guard(pmm_state.interrupt_lock);

        size_t core_id = 0;

        if (cpu::CPUCoreManager::initialized()) {
            core_id = cpu::CPUCoreManager::get_curr_cpu_id();
        }

        if (core_id < pmm_state.num_cpus) {
            PerCPUCache& cache = pmm_state.cpus[core_id];

            if (cache.count >= cache.capacity) {
                LockGuard _(pmm_state.lock);
                cache_flush(cache);
            }

            cache.stack[cache.count++] = reinterpret_cast<uintptr_t>(ptr);
            return;
        }
    }

    LockGuard guard(pmm_state.lock);

    // Multi-page free: go directly to bitmap.
    free_to_bitmap(reinterpret_cast<uintptr_t>(ptr) / PAGE_SIZE_4K, count);
    // LOG_DEBUG("PMM free range addr=%p count=%zu used_pages=%zu", ptr, count,
    // pmm_state.used_pages);
}

void PhysicalManager::reclaim_type(size_t memmap_type) {
    // Reclaim all regions of a specific Limine memmap type.
    for (size_t i = 0; i < memmap_request.response->entry_count; ++i) {
        limine_memmap_entry* entry = memmap_request.response->entries[i];

        if (entry->type == memmap_type) {
            uintptr_t base_aligned = align_up(entry->base, PAGE_SIZE_4K);
            uintptr_t end_aligned  = align_down(entry->base + entry->length, PAGE_SIZE_4K);

            if (end_aligned > base_aligned) {
                size_t pages = (end_aligned - base_aligned) / PAGE_SIZE_4K;
                free(reinterpret_cast<void*>(base_aligned), pages);
                LOG_INFO("PMM reclaimed type=%zu base=0x%lx pages=%zu", memmap_type, base_aligned,
                         pages);
            }
        }
    }
}

PMMStats PhysicalManager::get_stats() {
    LockGuard guard(pmm_state.lock);

    size_t cached_total = 0;

    for (size_t i = 0; i < pmm_state.num_cpus; ++i) {
        cached_total += pmm_state.cpus[i].count;
    }

    size_t actual_used = pmm_state.used_pages - cached_total;

    PMMStats stats     = {};
    stats.total_memory = pmm_state.total_pages * PAGE_SIZE_4K;
    stats.used_memory  = actual_used * PAGE_SIZE_4K;
    stats.free_memory  = (pmm_state.total_pages - actual_used) * PAGE_SIZE_4K;

    return stats;
}

void PhysicalManager::init() {
    if (!memmap_request.response || !memmap_request.response->entries) {
        PANIC("Error in Limine Memory Map");
    }

    limine_memmap_entry** memmaps = memmap_request.response->entries;
    size_t memmap_count           = memmap_request.response->entry_count;

    // Ensure lock is in a known unlocked state; PMM init is single-threaded
    // but the runtime code assumes the lock is usable afterwards.
    pmm_state.lock.unlock();

    if (mp_request.response && (mp_request.response->cpu_count > 0)) {
        pmm_state.num_cpus = mp_request.response->cpu_count;
    } else {
        pmm_state.num_cpus = 1;
    }

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

    pmm_state.total_pages           = div_roundup(highest_addr, PAGE_SIZE_4K);
    pmm_state.low_mem_threshold_idx = (PAGE_SIZE_1G * 4) / PAGE_SIZE_4K;

    // Clam threshold if system has less than 4GB RAM
    if (pmm_state.low_mem_threshold_idx > pmm_state.total_pages) {
        pmm_state.low_mem_threshold_idx = pmm_state.total_pages;
    }

    LOG_INFO("PMM: highest_addr=0x%lx total_pages=%zu low_memory_threshold_page=%zu", highest_addr,
             pmm_state.total_pages, pmm_state.low_mem_threshold_idx);

    size_t bitmap_bytes      = align_up(div_roundup(pmm_state.total_pages, 8u), 8u);
    pmm_state.bitmap_entries = bitmap_bytes / 8;

    // Summary bitmap: one bit per bitmap entry (i.e., per 64‑page block).
    size_t summary_bits       = pmm_state.bitmap_entries;
    size_t summary_bytes      = align_up(div_roundup(summary_bits, 8u), 8u);
    pmm_state.summary_entries = summary_bytes / 8;

    // `N` CPU stack cache size (for per cpu cache).
    size_t structs_byte = pmm_state.num_cpus * sizeof(PerCPUCache);
    size_t stack_bytes  = pmm_state.num_cpus * (CACHE_SIZE * sizeof(uintptr_t));

    size_t total_metadata_bytes = bitmap_bytes + summary_bytes + structs_byte + stack_bytes;

    LOG_DEBUG(
        "PMM: bitmap_bytes=%zu summary_bytes=%zu cpu_cache_bytes=%zu stack_bytes=%zu "
        "metadata_total=%zu",
        bitmap_bytes, summary_bytes, structs_byte, stack_bytes, total_metadata_bytes);

    // Find suitable hole for metadata. The idea is to place metadata in a
    // contiguous region that we then remove from the general pool, so the
    // allocator never hands it out by accident.
    limine_memmap_entry* best_candidate = nullptr;

    for (size_t i = 0; i < memmap_count; ++i) {
        limine_memmap_entry* entry = memmaps[i];

        // Reject 0x0 base
        if (entry->base == 0) {
            continue;
        }

        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= total_metadata_bytes) {
            if (!best_candidate || (entry->base > best_candidate->base)) {
                best_candidate = entry;
            }
        }
    }

    if (!best_candidate) {
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
    LOG_INFO("PMM: using metadata base phys=0x%lx (region base=0x%lx len=0x%lx)", meta_base,
             best_candidate->base, best_candidate->length);

    if (meta_base == 0) {
        meta_base += PAGE_SIZE_4K;
        best_candidate->length -= PAGE_SIZE_4K;

        if (best_candidate->length < total_metadata_bytes) {
            PANIC("No suitable memory hole found for metadata of size 0x%lx", total_metadata_bytes);
        }
    }

    // Reserve metadata region: we move the Limine entry forward so the
    // rest of the kernel never sees that region as free RAM.
    void* metadata_phys = reinterpret_cast<void*>(meta_base);
    best_candidate->base += total_metadata_bytes;
    best_candidate->length -= total_metadata_bytes;

    uintptr_t metadata_virt_addr = to_higher_half(reinterpret_cast<uintptr_t>(metadata_phys));

    // Layout: [bitmap][summary bitmap][stack cache]
    pmm_state.bitmap = reinterpret_cast<uintptr_t*>(metadata_virt_addr);

    pmm_state.summary_bitmap = reinterpret_cast<uintptr_t*>(metadata_virt_addr + bitmap_bytes);

    pmm_state.cpus =
        reinterpret_cast<PerCPUCache*>(metadata_virt_addr + bitmap_bytes + summary_bytes);

    uintptr_t* stack_data_start = reinterpret_cast<uintptr_t*>(metadata_virt_addr + bitmap_bytes +
                                                               summary_bytes + structs_byte);

    for (size_t i = 0; i < pmm_state.num_cpus; ++i) {
        pmm_state.cpus[i].count    = 0;
        pmm_state.cpus[i].capacity = CACHE_SIZE;
        pmm_state.cpus[i].stack    = stack_data_start + (i * CACHE_SIZE);
    }

    LOG_DEBUG(
        "PMM: bitmap@%p (%zu entries), summary@%p (%zu entries), cpu cache@%p (%zu cpu caches)",
        pmm_state.bitmap, pmm_state.bitmap_entries, pmm_state.summary_bitmap,
        pmm_state.summary_entries, pmm_state.cpus, pmm_state.num_cpus);

    // Initially mark all pages as used; we will then free only the
    // ranges that Limine reports as usable. This ensures we never
    // accidentally treat "unknown" memory as allocatable.
    memset(pmm_state.bitmap, 0xFF, bitmap_bytes);
    memset(pmm_state.summary_bitmap, 0xFF, summary_bytes);
    pmm_state.used_pages = pmm_state.total_pages;

    if (pmm_state.total_pages > pmm_state.low_mem_threshold_idx) {
        pmm_state.free_idx_hint = pmm_state.low_mem_threshold_idx;
    } else {
        pmm_state.free_idx_hint = 0;
    }

    pmm_state.aligned_index_hint = pmm_state.free_idx_hint;

    // Populate free memory from Limine map by freeing all usable pages.
    size_t reclaimed_pages = 0;
    for (size_t i = 0; i < memmap_count; ++i) {
        limine_memmap_entry* entry = memmaps[i];

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uintptr_t base = entry->base;
            size_t len     = entry->length;

            if (base == 0) {
                if (len >= PAGE_SIZE_4K) {
                    base += PAGE_SIZE_4K;
                    len -= PAGE_SIZE_4K;
                } else {
                    continue;
                }
            }

            size_t pages = len / PAGE_SIZE_4K;
            reclaimed_pages += pages;

            if (len > 0) {
                free(reinterpret_cast<void*>(base), pages);
            }
        }
    }

    // Restore `best_candidate` to original size
    best_candidate->base -= total_metadata_bytes;
    best_candidate->length += total_metadata_bytes;

    PMMStats stats = get_stats();
    LOG_INFO("PMM initialized: total_pages=%zu (~%zu MiB), reclaimed=%zu pages, free=%zu MiB",
             pmm_state.total_pages, (pmm_state.total_pages * PAGE_SIZE_4K) >> 20, reclaimed_pages,
             stats.free_memory >> 20);
}

}  // namespace kernel::memory