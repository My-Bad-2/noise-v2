#include "memory/vma.hpp"
#include "memory/memory.hpp"
#include "memory/pmm.hpp"
#include "libs/log.hpp"
#include "libs/math.hpp"

namespace kernel::memory {
void VirtualAllocator::expand_pool() {
    // Grow the node pool by allocating one physical page and treating it
    // as an array of `VmFreeRegion` nodes. Nodes are then threaded into
    // a free-node singly-linked list.
    void* pool_phys = PhysicalManager::alloc();

    if (pool_phys == nullptr) {
        PANIC("Virtual Allocator Out of Memory - Cannot expand node pool!");
    }

    VmFreeRegion* pool_base = static_cast<VmFreeRegion*>(to_higher_half(pool_phys));
    size_t node_count       = PAGE_SIZE_4K / sizeof(VmFreeRegion);

    for (size_t i = 0; i < node_count - 1; ++i) {
        pool_base[i].next = &pool_base[i + 1];
    }

    pool_base[node_count - 1].next = this->free_nodes_head;
    this->free_nodes_head          = &pool_base[0];

    LOG_INFO("VirtualAllocator: expanded node pool by %zu nodes", node_count);
}

VmFreeRegion* VirtualAllocator::new_node() {
    // Node pool is grown lazily on demand so that idle systems don't
    // waste physical pages on metadata.
    if (this->free_nodes_head == nullptr) {
        expand_pool();

        if (this->free_nodes_head == nullptr) {
            LOG_ERROR("VirtualAllocator: new_node failed after expand_pool");
            return nullptr;
        }
    }

    VmFreeRegion* node = free_nodes_head;
    free_nodes_head    = free_nodes_head->next;

    // Reset node contents to a known state before use.
    node->start  = 0;
    node->length = 0;
    node->next   = nullptr;

    return node;
}

void VirtualAllocator::return_node(VmFreeRegion* node) {
    // Return metadata to the free-node pool instead of freeing pages:
    // we keep node pages around to serve future allocations.
    node->next      = this->free_nodes_head;
    free_nodes_head = node;
}

uintptr_t VirtualAllocator::alloc_region(size_t size, size_t align) {
    // First-fit search over a sorted list of free ranges. The alignment
    // constraint may force us to split a free region into up to two
    // pieces (prefix, suffix) around the allocated block.
    VmFreeRegion* curr = this->region_head;
    VmFreeRegion* prev = nullptr;

    while (curr) {
        uintptr_t aligned_addr = align_up(curr->start, align);
        uintptr_t alloc_end    = aligned_addr + size;
        uintptr_t curr_end     = curr->start + curr->length;

        if (alloc_end <= curr_end) {
            // The requested region fits inside `curr`.
            if (aligned_addr > curr->start) {
                // Case 1: we leave a prefix and maybe a suffix.
                uintptr_t tail_size = curr_end - alloc_end;

                if (tail_size > 0) {
                    // Create a tail region after the allocated block.
                    VmFreeRegion* tail = new_node();

                    if (!tail) {
                        LOG_ERROR("VirtualAllocator: failed to allocate tail node");
                        return 0;
                    }

                    tail->start  = alloc_end;
                    tail->length = tail_size;
                    tail->next   = curr->next;

                    curr->next = tail;
                }

                // Shrink the current region to just the prefix.
                curr->length = aligned_addr - curr->start;

                // LOG_DEBUG("VirtualAllocator: alloc_region size=0x%zx align=0x%zx -> 0x%lx
                // (split)",
                //   size, align, aligned_addr);
                return aligned_addr;
            } else {
                // Case 2: allocation starts at the beginning of `curr`.
                size_t tail_size = curr_end - alloc_end;
                uintptr_t res    = curr->start;

                if (tail_size == 0) {
                    // Entire region consumed; unlink it from the list.
                    if (prev) {
                        prev->next = curr->next;
                    } else {
                        this->region_head = curr->next;
                    }

                    return_node(curr);
                } else {
                    // Leave only the tail as the new free region.
                    curr->start  = alloc_end;
                    curr->length = tail_size;
                }

                // LOG_DEBUG("VirtualAllocator: alloc_region size=0x%zx align=0x%zx -> 0x%lx
                // (trim)",
                //   size, align, res);
                return res;
            }
        }

        prev = curr;
        curr = curr->next;
    }

    LOG_WARN("VirtualAllocator: alloc_region failed size=0x%zx align=0x%zx", size, align);
    return 0;
}

void VirtualAllocator::free_region(uintptr_t start, size_t size) {
    // Insert a newly freed region back into the sorted list and then
    // eagerly coalesce with neighboring regions to combat fragmentation.
    VmFreeRegion* node = new_node();

    if (!node) {
        LOG_ERROR("VirtualAllocator: free_region cannot allocate node (leak of virt range)");
        return;
    }

    node->start  = start;
    node->length = size;

    VmFreeRegion* curr = this->region_head;
    VmFreeRegion* prev = nullptr;

    while (curr && curr->start < start) {
        prev = curr;
        curr = curr->next;
    }

    if (prev) {
        prev->next = node;
        node->next = curr;
    } else {
        node->next        = this->region_head;
        this->region_head = node;
    }

    // Coalesce with right neighbor if directly adjacent.
    if (node->next && (node->start + node->length == node->next->start)) {
        VmFreeRegion* victim = node->next;
        node->length += victim->length;
        node->next = victim->next;
        return_node(victim);
    }

    // Coalesce with left neighbor if directly adjacent.
    if (prev && (prev->start + prev->length == node->start)) {
        prev->length += node->length;
        prev->next = node->next;
        return_node(node);
    }

    // LOG_DEBUG("VirtualAllocator: free_region start=0x%lx size=0x%zx", start, size);
}

void VirtualAllocator::init(uintptr_t start, size_t length) {
    // Bootstrap the allocator with one big free region that covers the
    // entire virtual heap range. Node pool is pre-expanded once here.
    expand_pool();

    this->region_head = this->new_node();
    if (!this->region_head) {
        PANIC("VirtualAllocator: failed to initialize region head");
    }

    this->region_head->start  = start;
    this->region_head->length = length;
    this->region_head->next   = nullptr;

    LOG_INFO("VirtualAllocator: initialized region [0x%lx, 0x%lx)", start, start + length);
}
}  // namespace kernel::memory