#include "memory/vma.hpp"
#include "boot/boot.h"
#include "memory/pmm.hpp"
#include "libs/math.hpp"
#include "hal/smp_manager.hpp"

namespace kernel::memory {
VmRegion* VmRegionAllocator::allocate() {
    LockGuard guard(this->lock);

    if (!this->free_head) {
        this->refill();
    }

    VmRegion* node  = reinterpret_cast<VmRegion*>(this->free_head);
    this->free_head = this->free_head->next;

    node->parent = node->left = node->right = nullptr;

    return node;
}

void VmRegionAllocator::deallocate(VmRegion* node) {
    LockGuard guard(this->lock);

    reinterpret_cast<FreeNode*>(node)->next = this->free_head;
    this->free_head                         = reinterpret_cast<FreeNode*>(node);
}

void VmRegionAllocator::refill() {
    void* phys          = PhysicalManager::alloc();
    uintptr_t virt_base = to_higher_half(reinterpret_cast<uintptr_t>(phys));

    size_t count    = PAGE_SIZE_4K / sizeof(VmRegion);
    VmRegion* nodes = reinterpret_cast<VmRegion*>(virt_base);

    for (size_t i = 0; i < count; ++i) {
        reinterpret_cast<FreeNode*>(&nodes[i])->next = this->free_head;
        this->free_head                              = reinterpret_cast<FreeNode*>(&nodes[i]);
    }
}

void VirtualMemoryAllocator::init(uintptr_t start_addr) {
    this->cpu_count = mp_request.response->cpu_count;

    size_t cache_size       = sizeof(CpuCache) * this->cpu_count;
    uintptr_t aligned_start = align_up(start_addr, PAGE_SIZE_1G);

    uintptr_t curr_addr = aligned_start;
    uintptr_t cache_end = aligned_start + cache_size;

    while (curr_addr < cache_end) {
        void* phys = PhysicalManager::alloc();
        PageMap::get_kernel_map()->map(curr_addr, reinterpret_cast<uintptr_t>(phys), Read | Write,
                                       CacheType::WriteBack, PageSize::Size4K);

        curr_addr += PAGE_SIZE_4K;
    }

    this->caches = reinterpret_cast<CpuCache*>(aligned_start);
    for (size_t i = 0; i < this->cpu_count; ++i) {
        this->caches[i].count = 0;
    }

    this->heap_base = align_up(cache_end, PAGE_SIZE_4K);
    this->root      = nullptr;
}

void* VirtualMemoryAllocator::allocate(size_t size, uint8_t flags, CacheType cache) {
    if (size == PAGE_SIZE_4K) {
        uint32_t cpu   = 0;
        auto& core_mgr = cpu::CpuCoreManager::get();

        if (core_mgr.initialized()) {
            cpu = core_mgr.get_current_core()->core_idx;
        }

        if ((cpu >= 0) && (static_cast<size_t>(cpu) < this->cpu_count)) {
            CpuCache& cpu_cache = this->caches[cpu];

            cpu_cache.lock.lock();

            if (cpu_cache.count > 0) {
                uintptr_t virt_addr = cpu_cache.va_holes[--cpu_cache.count];
                cpu_cache.lock.unlock();

                this->map(virt_addr, size, flags, cache);
                this->insert_region(virt_addr, size, flags, cache);
                return reinterpret_cast<void*>(virt_addr);
            }

            cpu_cache.lock.unlock();
        }
    }

    this->lock.lock();

    size_t align = PAGE_SIZE_4K;

    if (size >= PAGE_SIZE_1G) {
        align = PAGE_SIZE_1G;
    } else if (size >= PAGE_SIZE_2M) {
        align = PAGE_SIZE_2M;
    }

    uintptr_t virt_addr = this->find_hole(size, align);
    this->insert_region_locked(virt_addr, size, flags, cache);

    this->lock.unlock();

    this->map(virt_addr, size, flags, cache);
    return reinterpret_cast<void*>(virt_addr);
}

void* VirtualMemoryAllocator::reserve(size_t size, size_t alignment, uint8_t flags) {
    this->lock.lock();

    uintptr_t virt_addr = this->find_hole(size, alignment);
    this->insert_region_locked(virt_addr, size, flags, CacheType::Uncached);

    this->lock.unlock();

    return reinterpret_cast<void*>(virt_addr);
}

void VirtualMemoryAllocator::free(void* ptr, bool free_phys) {
    if (!ptr) {
        return;
    }

    uintptr_t virt_addr = reinterpret_cast<uintptr_t>(ptr);

    this->lock.lock();
    VmRegion* node = this->find_node(virt_addr);

    if (!node) {
        this->lock.unlock();
        return;
    }

    size_t size = node->size;

    this->delete_node_locked(node);
    this->lock.unlock();

    this->unmap(virt_addr, size, free_phys);

    if (free_phys && size == PAGE_SIZE_4K) {
        uint32_t cpu   = 0;
        auto& core_mgr = cpu::CpuCoreManager::get();

        if (core_mgr.initialized()) {
            cpu = core_mgr.get_current_core()->core_idx;
        }

        if ((cpu >= 0) && (static_cast<size_t>(cpu) < this->cpu_count)) {
            CpuCache& cache = this->caches[cpu];

            cache.lock.lock();

            if (cache.count < CpuCache::CAPACITY) {
                cache.va_holes[cache.count++] = virt_addr;
            }

            cache.lock.unlock();
        }
    }
}

void VirtualMemoryAllocator::map(uintptr_t virt_addr, size_t size, uint8_t flags, CacheType cache) {
    auto* kmap = PageMap::get_kernel_map();

    uintptr_t curr = virt_addr;
    uintptr_t end  = virt_addr + size;

    while (curr < end) {
        size_t remaining = end - curr;

        if ((remaining >= PAGE_SIZE_1G) && (curr % PAGE_SIZE_1G == 0)) {
            kmap->map(curr, flags, cache, PageSize::Size1G);
            curr += PAGE_SIZE_1G;
        } else if ((remaining >= PAGE_SIZE_2M) && (curr % PAGE_SIZE_2M == 0)) {
            kmap->map(curr, flags, cache, PageSize::Size2M);
            curr += PAGE_SIZE_2M;
        } else {
            kmap->map(curr, flags, cache, PageSize::Size4K);
            curr += PAGE_SIZE_4K;
        }
    }
}

void VirtualMemoryAllocator::unmap(uintptr_t virt_addr, size_t size, bool free_phys) {
    auto* kmap = PageMap::get_kernel_map();

    uintptr_t curr = virt_addr;
    uintptr_t end  = virt_addr + size;

    while (curr < end) {
        uintptr_t remaining = end - curr;
        size_t step         = PAGE_SIZE_4K;

        if ((curr % PAGE_SIZE_1G == 0) && (remaining >= PAGE_SIZE_1G)) {
            step = PAGE_SIZE_1G;
        } else if ((curr % PAGE_SIZE_2M == 0) && (remaining >= PAGE_SIZE_2M)) {
            step = PAGE_SIZE_2M;
        }

        kmap->unmap(curr, 0, free_phys);
        curr += step;
    }
}

VmRegion* VirtualMemoryAllocator::find_node(uintptr_t start) {
    VmRegion* curr = this->root;

    while (curr) {
        if (start == curr->start) {
            return curr;
        }

        if (start < curr->start) {
            curr = curr->left;
        } else {
            curr = curr->right;
        }
    }

    return nullptr;
}

uintptr_t VirtualMemoryAllocator::find_hole(size_t size, size_t alignment) {
    if (this->cached_cursor) {
        // Calculate where the allocation would start if placed after the cursor
        uintptr_t candidate = align_up(this->cached_cursor->end(), alignment);
        size_t overhead     = candidate - this->cached_cursor->end();

        // Does the gap after the cursor fit the request?
        if (this->cached_cursor->gap >= (size + overhead)) {
            // Voila! We found a spot without touching the tree.
            return candidate;
        }
    }

    // Try to find a hole inside the tree
    uintptr_t found = this->find_hole(this->root, size, alignment);
    if (found != 0) {
        return found;
    }

    VmRegion* max = this->root;
    if (!max) {
        // Tree is empty, return aligned base
        return align_up(heap_base, alignment);
    }

    // Find the very last node.
    while (max->right) {
        max = max->right;
    }

    // Calculate valid address after the last node
    return align_up(max->end(), alignment);
}

uintptr_t VirtualMemoryAllocator::find_hole(VmRegion* node, size_t size, size_t alignment) {
    if (!node) {
        return 0;
    }

    // If the largest anywhere in this subtree is smaller than requested size,
    // it is impossible to find a fit. Backtrack immediately.
    if (node->subtree_max_gap < size) {
        return 0;
    }

    // Prefer lower addresses. If the left subtree might have a gap,
    // look there.
    if (node->left && node->left->subtree_max_gap >= size) {
        uintptr_t left = this->find_hole(node->left, size, alignment);

        if (left != 0) {
            return left;
        }
    }

    uintptr_t candidate   = align_up(node->end(), alignment);
    size_t align_overhead = candidate - node->end();

    // Ensure that the gap is physically large enoguht to hold (size + alignment adjustment)
    if (node->gap >= (size + align_overhead)) {
        return candidate;
    }

    // We failed, try right.
    if (node->right && node->right->subtree_max_gap >= size) {
        return this->find_hole(node->right, size, alignment);
    }

    // We failed, Mr. Stark
    return 0;
}

void VirtualMemoryAllocator::insert_region(uintptr_t start, size_t size, uint8_t flags,
                                           CacheType cache) {
    LockGuard guard(this->lock);
    this->insert_region_locked(start, size, flags, cache);
}

void VirtualMemoryAllocator::insert_region_locked(uintptr_t start, size_t size, uint8_t flags,
                                                  CacheType cache) {
    VmRegion* z = this->metadata_allocator.allocate();
    z->start    = start;
    z->size     = size;
    z->flags    = flags;
    z->cache    = cache;
    z->is_red   = true;
    z->left = z->right = nullptr;

    VmRegion* y = nullptr;
    VmRegion* x = this->root;

    while (x) {
        y = x;

        if (z->start < x->start) {
            x = x->left;
        } else {
            x = x->right;
        }
    }

    z->parent = y;

    if (!y) {
        this->root = z;
    } else if (z->start < y->start) {
        y->left = z;
    } else {
        y->right = z;
    }

    // We need to find the node strictly before Z (predecssor) and
    // strictly after Z (successor) in address order to set the gaps
    // correctly.
    VmRegion* pred = this->predecessor(z);
    VmRegion* succ = this->successor(z);

    if (succ) {
        // Distance to successor
        z->gap = succ->start - z->end();
    } else {
        // Z is the last node in memory. Gap is effectively infinite
        z->gap = static_cast<uintptr_t>(-1) - z->end();
    }

    // Distance to Z
    if (pred) {
        pred->gap = z->start - pred->end();
        this->update_path_to_root(pred);
    }

    this->update_path_to_root(z);
    this->insert_fixup(z);
    this->cached_cursor = z;
}

void VirtualMemoryAllocator::delete_node_locked(VmRegion* z) {
    // Since we are about to remove the logical range z. We must
    // merge z's gap into its predessor's gap.
    // Now new Gap for Pred = (gap before z) + (z's size) + (gap after z)
    VmRegion* pred = this->predecessor(z);
    if (pred) {
        pred->gap += z->size + z->gap;
    }

    VmRegion *x, *y;
    bool original_y_red = false;

    if (!z->left || !z->right) {
        y = z;
    } else {
        y = z->right;

        while (y->left) {
            y = y->left;
        }
    }

    // Capture y's parent before we move pointers, so we know where
    // to start updating metadata
    VmRegion* update_start = y->parent;

    original_y_red = y->is_red;
    if (y->left) {
        x = y->left;
    } else {
        x = y->right;
    }

    if (x) {
        x->parent = y->parent;
    }

    if (!y->parent) {
        root = x;
    } else if (y == y->parent->left) {
        y->parent->left = x;
    } else {
        y->parent->right = x;
    }

    if (y != z) {
        z->start = y->start;
        z->size  = y->size;
        z->flags = y->flags;
        z->cache = y->cache;

        // z now represents the region y used to hold.
        z->gap = y->gap;
    }

    if (this->cached_cursor == z || this->cached_cursor == y) {
        this->cached_cursor = pred;
    }

    if (!original_y_red && x) {
        this->delete_fixup(x);
    }

    if (update_start) {
        this->update_path_to_root(update_start);
    }

    if (pred) {
        this->update_path_to_root(pred);
    }

    this->metadata_allocator.deallocate(y);
}

void VirtualMemoryAllocator::rotate_left(VmRegion* x) {
    VmRegion* y = x->right;
    x->right    = y->left;

    if (y->left) {
        y->left->parent = x;
    }

    y->parent = x->parent;

    if (!x->parent) {
        root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }

    y->left   = x;
    x->parent = y;

    this->update_node_metadata(x);
    this->update_node_metadata(y);
}

void VirtualMemoryAllocator::rotate_right(VmRegion* x) {
    VmRegion* y = x->left;
    x->left     = y->right;

    if (y->right) {
        y->right->parent = x;
    }

    y->parent = x->parent;

    if (!x->parent) {
        root = y;
    } else if (x == x->parent->right) {
        x->parent->right = y;
    } else {
        x->parent->left = y;
    }

    y->right  = x;
    x->parent = y;

    this->update_node_metadata(x);
    this->update_node_metadata(y);
}

void VirtualMemoryAllocator::insert_fixup(VmRegion* z) {
    while (z->parent && z->parent->is_red) {
        if (z->parent == z->parent->parent->left) {
            VmRegion* y = z->parent->parent->right;

            if (y && y->is_red) {
                z->parent->is_red         = false;
                y->is_red                 = false;
                z->parent->parent->is_red = true;
                z                         = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    z = z->parent;
                    this->rotate_left(z);
                }

                z->parent->is_red         = false;
                z->parent->parent->is_red = true;
                this->rotate_right(z->parent->parent);
            }
        } else {
            VmRegion* y = z->parent->parent->left;

            if (y && y->is_red) {
                z->parent->is_red         = false;
                y->is_red                 = false;
                z->parent->parent->is_red = true;
                z                         = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    this->rotate_right(z);
                }

                z->parent->is_red         = false;
                z->parent->parent->is_red = true;
                this->rotate_left(z->parent->parent);
            }
        }
    }

    this->root->is_red = false;
}

void VirtualMemoryAllocator::delete_fixup(VmRegion* x) {
    while (x != root && !x->is_red) {
        if (x == x->parent->left) {
            VmRegion* w = x->parent->right;

            if (w->is_red) {
                w->is_red         = false;
                x->parent->is_red = true;
                this->rotate_left(x->parent);
                w = x->parent->right;
            }

            if ((!w->left || !w->left->is_red) && (!w->right || !w->right->is_red)) {
                w->is_red = true;
                x         = x->parent;
            } else {
                if (!w->right || !w->right->is_red) {
                    if (w->left) {
                        w->left->is_red = false;
                    }

                    w->is_red = true;
                    this->rotate_right(w);
                    w = x->parent->right;
                }

                w->is_red         = x->parent->is_red;
                x->parent->is_red = false;

                if (w->right) {
                    w->right->is_red = false;
                }

                this->rotate_left(x->parent);
                x = root;
            }
        } else {
            VmRegion* w = x->parent->left;

            if (w->is_red) {
                w->is_red         = false;
                x->parent->is_red = true;
                this->rotate_right(x->parent);
                w = x->parent->left;
            }

            if ((!w->right || !w->right->is_red) && (!w->left || !w->left->is_red)) {
                w->is_red = true;
                x         = x->parent;
            } else {
                if (!w->left || !w->left->is_red) {
                    if (w->right) {
                        w->right->is_red = false;
                    }

                    w->is_red = true;
                    this->rotate_left(w);
                    w = x->parent->left;
                }

                w->is_red         = x->parent->is_red;
                x->parent->is_red = false;

                if (w->left) {
                    w->left->is_red = false;
                }

                this->rotate_right(x->parent);
                x = root;
            }
        }
    }

    if (x) {
        x->is_red = false;
    }
}

void VirtualMemoryAllocator::update_node_metadata(VmRegion* x) {
    if (!x) {
        return;
    }

    size_t left_max  = x->left ? x->left->subtree_max_gap : 0;
    size_t right_max = x->right ? x->right->subtree_max_gap : 0;

    size_t curr_max = left_max;

    if (right_max > curr_max) {
        curr_max = right_max;
    }

    if (x->gap > curr_max) {
        curr_max = x->gap;
    }

    x->subtree_max_gap = curr_max;
}

VmRegion* VirtualMemoryAllocator::predecessor(VmRegion* node) {
    // If the left subtree is not null, the successor is the
    // rightmost (maximum) node in the left subtree.
    if (node->left) {
        node = node->left;

        while (node->right) {
            node = node->right;
        }

        return node;
    }

    // If the left subtree is null, the successor is the lowest
    // ancestor for which 'x' is in the right subtree. We walk up
    // the tree until we are no longer a left child.
    VmRegion* p = node->parent;
    while (p && node == p->left) {
        node = p;
        p    = p->parent;
    }

    return p;
}

VmRegion* VirtualMemoryAllocator::successor(VmRegion* node) {
    // If the right subtree is not null, the successor is the
    // leftmost (minimum) node in the right subtree.
    if (node->right) {
        node = node->right;

        while (node->left) {
            node = node->left;
        }

        return node;
    }

    // If the right subtree is null, the successor is the lowest
    // ancestor for which 'x' is in the left subtree. We walk up
    // the tree until we are no longer a right child.
    VmRegion* p = node->parent;
    while (p && node == p->right) {
        node = p;
        p    = p->parent;
    }

    return p;
}

void VirtualMemoryAllocator::update_path_to_root(VmRegion* x) {
    while (x) {
        this->update_node_metadata(x);
        x = x->parent;
    }
}
}  // namespace kernel::memory