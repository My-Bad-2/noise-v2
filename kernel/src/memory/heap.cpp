#include "libs/log.hpp"
#include "memory/memory.hpp"
#include "memory/vmm.hpp"
#include "memory/heap.hpp"
#include "libs/math.hpp"

#define BLOCK_MAGIC 0xC0FFEE

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

namespace kernel::memory {
namespace {
KernelHeap kheap;
}

size_t KernelHeap::align(size_t n) {
    return align_up(n, 0x10u);
}

void KernelHeap::insert_free_node(BlockHeader* block) {
    // Free-list is a simple doubly-linked list with head insertion; this
    // keeps insertion O(1) and keeps recently-freed blocks hot.
    block->next_free = this->free_list_head;
    block->prev_free = nullptr;

    if (this->free_list_head) {
        this->free_list_head->prev_free = block;
    }

    this->free_list_head = block;
}

void KernelHeap::remove_free_node(BlockHeader* block) {
    if (block->prev_free) {
        block->prev_free->next_free = block->next_free;
    } else {
        this->free_list_head = block->next_free;
    }

    if (block->next_free) {
        block->next_free->prev_free = block->prev_free;
    }

    block->next_free = nullptr;
    block->prev_free = nullptr;
}

bool KernelHeap::expand_heap(size_t size_needed) {
    // Expansion policy: bias toward larger pages to reduce page-table
    // pressure, but fall back to 4 KiB pages if necessary.
    size_t overhead  = sizeof(BlockHeader);
    size_t total_req = size_needed + overhead;

    size_t count = 0;
    PageSize size;

    if (total_req >= PAGE_SIZE_1G) {
        count = div_roundup(total_req, PAGE_SIZE_1G);
        size  = PageSize::Size1G;
    } else if (total_req >= PAGE_SIZE_2M) {
        count = div_roundup(total_req, PAGE_SIZE_2M);
        size  = PageSize::Size2M;
    } else {
        // Default to expanding by 2 MiB even for smaller requests to keep
        // the number of heap regions low and amortize region overhead.
        count = 1;
        size  = PageSize::Size2M;
    }

    size_t alloc_bytes = count * static_cast<size_t>(size);

    void* region_start = VirtualManager::allocate(count, size);

    if (!region_start) {
        // Fallback: if large pages fail (e.g. fragmentation), try 4 KiB
        // pages instead of immediately giving up.
        if (size > PageSize::Size4K) {
            size         = PageSize::Size4K;
            count        = div_roundup(total_req, PAGE_SIZE_4K);
            alloc_bytes  = count * static_cast<size_t>(size);
            region_start = VirtualManager::allocate(count, size);
            if (!region_start) {
                LOG_ERROR("KernelHeap: expand_heap failed (size_needed=%zu)", size_needed);
                return false;
            }
        } else {
            LOG_ERROR("KernelHeap: expand_heap failed (size_needed=%zu)", size_needed);
            return false;
        }
    }

    BlockHeader* new_block = reinterpret_cast<BlockHeader*>(region_start);
    new_block->magic       = BLOCK_MAGIC;
    new_block->size        = alloc_bytes - overhead;
    new_block->is_free     = true;

    new_block->region_size = static_cast<size_t>(size);

    new_block->next = nullptr;
    new_block->prev = nullptr;

    this->insert_free_node(new_block);

    LOG_INFO("KernelHeap: expanded heap by %zu bytes (page_size=%zu, count=%zu)", alloc_bytes,
             static_cast<size_t>(size), count);
    return true;
}

void KernelHeap::coalesce(BlockHeader* block) {
    // Coalesce forward into the next block when it is free: this helps
    // rebuild large contiguous chunks for future big allocations.
    if (block->next && block->next->is_free) {
        BlockHeader* next_block = block->next;
        this->remove_free_node(next_block);

        block->size += sizeof(BlockHeader) + next_block->size;
        block->next = next_block->next;

        if (block->next) {
            block->next->prev = block;
        }
    }

    // Coalesce backward into the previous block when it is free: this
    // keeps physical-order list compact and improves region-release odds.
    if (block->prev && block->prev->is_free) {
        BlockHeader* prev_block = block->prev;
        this->remove_free_node(block);

        prev_block->size += sizeof(BlockHeader) + block->size;
        prev_block->next = block->next;

        if (block->next) {
            block->next->prev = prev_block;
        }

        block = prev_block;
    }

    this->try_free_region(block);
}

void KernelHeap::try_free_region(BlockHeader* block) {
    // Policy: if a single free block spans an entire region (no
    // neighbors), we prefer to return it to the VMM. This keeps heap
    // footprint proportional to real usage.
    if (block->is_free && block->prev == nullptr && block->next == nullptr) {
        size_t total_size = block->size + sizeof(BlockHeader);

        this->remove_free_node(block);
        VirtualManager::free(reinterpret_cast<void*>(block), total_size);

        LOG_INFO("KernelHeap: released region back to VMM (size=%zu)", total_size);
    }
}

void* KernelHeap::alloc(size_t size) {
    if (size == 0) {
        return nullptr;
    }

    size = align(size);

    LockGuard guard(lock);

    // Best-fit search over the free-list. This minimizes leftover
    // fragment sizes at the cost of a linear scan.
    BlockHeader* best_fit = nullptr;
    BlockHeader* curr     = this->free_list_head;

    while (curr) {
        if (curr->size >= size) {
            if (!best_fit || curr->size < best_fit->size) {
                best_fit = curr;
            }

            if (curr->size == size) {
                break;
            }
        }

        curr = curr->next_free;
    }

    if (!best_fit) {
        // No suitable block; grow the heap and retry. We explicitly drop
        // the lock before recursive call to avoid deadlock.
        if (expand_heap(size)) {
            this->lock.unlock();
            return this->alloc(size);
        }

        LOG_ERROR("KernelHeap: alloc failed size=%zu (no block, cannot expand)", size);
        return nullptr;
    }

    this->remove_free_node(best_fit);
    best_fit->is_free = 0;

    // If the block is significantly larger than requested, split it into
    // an allocated part and a new free block. The "+ 0x10" ensures that
    // the remainder is big enough to be useful (beyond header + min obj).
    if (best_fit->size > size + sizeof(BlockHeader) + 0x10) {
        BlockHeader* new_block = reinterpret_cast<BlockHeader*>(reinterpret_cast<char*>(best_fit) +
                                                                sizeof(BlockHeader) + size);

        new_block->magic       = BLOCK_MAGIC;
        new_block->is_free     = true;
        new_block->size        = best_fit->size - size - sizeof(BlockHeader);
        new_block->region_size = best_fit->region_size;

        new_block->next = best_fit->next;
        new_block->prev = best_fit;

        if (best_fit->next) {
            best_fit->next->prev = new_block;
        }

        best_fit->next = new_block;
        best_fit->size = size;

        this->insert_free_node(new_block);
    }

    void* user_ptr =
        reinterpret_cast<void*>(reinterpret_cast<char*>(best_fit) + sizeof(BlockHeader));

    // LOG_DEBUG("KernelHeap: alloc size=%zu -> %p (block_size=%zu)", size, user_ptr,
    // best_fit->size);
    return user_ptr;
}

void KernelHeap::free(void* ptr) {
    if (!ptr) {
        return;
    }

    LockGuard guard(lock);

    BlockHeader* header =
        reinterpret_cast<BlockHeader*>(reinterpret_cast<char*>(ptr) - sizeof(BlockHeader));

    // Basic sanity check: if the magic doesn't match, we ignore the
    // free. This avoids crashing on double-frees or foreign pointers
    // but also hides bugs.
    if (unlikely(header->magic != BLOCK_MAGIC)) {
        LOG_ERROR("KernelHeap: free called with invalid block header %p", header);
        return;
    }

    if (header->is_free) {
        LOG_WARN("KernelHeap: double free detected for %p", ptr);
        return;
    }

    header->is_free = true;
    this->insert_free_node(header);

    // LOG_DEBUG("KernelHeap: free %p (size=%zu)", ptr, header->size);

    this->coalesce(header);
}

void kfree(void* ptr) {
    kheap.free(ptr);
}

void* kmalloc(size_t size) {
    return kheap.alloc(size);
}

void* aligned_kalloc(size_t size, size_t alignment) {
    if (alignment == 0 || !is_aligned(alignment, alignment)) {
        return nullptr;
    }

    size_t overhead = alignment + sizeof(void*);

    if (size > SIZE_MAX - overhead) {
        return nullptr;
    }

    void* raw_ptr = kheap.alloc(size + overhead);
    if (!raw_ptr) {
        return nullptr;
    }

    uintptr_t start_addr   = reinterpret_cast<uintptr_t>(raw_ptr) + sizeof(void*);
    uintptr_t aligned_addr = align_up(start_addr, alignment);

    void** stash_location = reinterpret_cast<void**>(aligned_addr - sizeof(void*));
    *stash_location       = raw_ptr;

    return reinterpret_cast<void*>(aligned_addr);
}

void aligned_kfree(void* ptr) {
    if (!ptr) {
        return;
    }

    uintptr_t addr        = reinterpret_cast<uintptr_t>(ptr);
    void** stash_location = reinterpret_cast<void**>(addr - sizeof(void*));
    void* raw_ptr         = *stash_location;

    kheap.free(raw_ptr);
}
}  // namespace kernel::memory