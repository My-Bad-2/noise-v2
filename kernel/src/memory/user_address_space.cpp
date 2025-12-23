#include "memory/memory.hpp"
#include "memory/pagemap.hpp"
#include "memory/pcid_manager.hpp"
#include "memory/user_address_space.hpp"
#include "libs/math.hpp"
#include "task/process.hpp"
#include "memory/pmm.hpp"

namespace kernel::memory {
UserVmRegion* UserVmRegionAllocator::allocate() {
    LockGuard guard(this->lock);

    if (!this->free_head) {
        this->refill();
    }

    UserVmRegion* node = reinterpret_cast<UserVmRegion*>(this->free_head);
    this->free_head    = this->free_head->next;

    node->parent = node->left = node->right = nullptr;

    return node;
}

void UserVmRegionAllocator::deallocate(UserVmRegion* node) {
    LockGuard guard(this->lock);

    reinterpret_cast<FreeNode*>(node)->next = this->free_head;
    this->free_head                         = reinterpret_cast<FreeNode*>(node);
}

void UserVmRegionAllocator::refill() {
    void* phys          = PhysicalManager::alloc();
    uintptr_t virt_base = to_higher_half(reinterpret_cast<uintptr_t>(phys));

    size_t count        = PAGE_SIZE_4K / sizeof(UserVmRegion);
    UserVmRegion* nodes = reinterpret_cast<UserVmRegion*>(virt_base);

    for (size_t i = 0; i < count; ++i) {
        reinterpret_cast<FreeNode*>(&nodes[i])->next = this->free_head;
        this->free_head                              = reinterpret_cast<FreeNode*>(&nodes[i]);
    }
}

void UserAddressSpace::init(task::Process* proc) {
    this->page_map      = proc->map;
    this->root          = nullptr;
    this->cached_cursor = nullptr;
}

UserAddressSpace::~UserAddressSpace() {
    LockGuard guard(this->mutex);

    this->free_tree(this->root);
    this->root          = nullptr;
    this->cached_cursor = nullptr;
}

void UserAddressSpace::free_tree(UserVmRegion* node) {
    if (!node) {
        return;
    }

    this->free_tree(node->left);
    this->free_tree(node->right);
    this->metadata_allocator.deallocate(node);
}

void* UserAddressSpace::allocate(size_t size, uint8_t flags, PageSize type) {
    LockGuard guard(this->mutex);

    if (size == 0) {
        return nullptr;
    }

    size_t alignment = PAGE_SIZE_4K;
    if (type == PageSize::Size2M) {
        alignment = PAGE_SIZE_2M;
    } else if (type == PageSize::Size1G) {
        alignment = PAGE_SIZE_1G;
    }

    // Force User Flag
    flags |= User;

    size = align_up(size, alignment);

    uintptr_t virt_addr = this->find_hole(size, alignment);

    if (virt_addr < USER_START || (virt_addr + size) > USER_END) {
        return nullptr;
    }

    this->insert_region(virt_addr, size, flags, CacheType::WriteBack, type);
    return reinterpret_cast<void*>(virt_addr);
}

bool UserAddressSpace::allocate_specific(uintptr_t virt_addr, size_t size, uint8_t flags,
                                         PageSize type) {
    LockGuard guard(this->mutex);

    if (size == 0) {
        return false;
    }

    size      = align_up(size, PAGE_SIZE_4K);
    virt_addr = align_down(virt_addr, PAGE_SIZE_4K);

    if (virt_addr < USER_START || (virt_addr + size) > USER_END) {
        return false;
    }

    if (this->check_overlap(virt_addr, size)) {
        return false;
    }

    this->insert_region(virt_addr, size, flags | User, CacheType::WriteBack, type);
    return true;
}

void UserAddressSpace::free(void* ptr) {
    LockGuard guard(this->mutex);

    uintptr_t virt_addr = reinterpret_cast<uintptr_t>(ptr);

    UserVmRegion* node = this->find_region_containing(virt_addr);

    // Only free if we found the exact starting address
    if (node && node->start == virt_addr) {
        // Unmap physical frames and flush TLB
        uint16_t pcid = PcidManager::get().get_pcid(this->process);

        this->page_map->unmap(node->start, pcid, true);
        this->delete_node(node);
    }
}

uintptr_t UserAddressSpace::find_hole(size_t size, size_t alignment) {
    // If we recently allocated, try to append immediately after
    if (this->cached_cursor) {
        uintptr_t candidate = align_up(this->cached_cursor->end(), alignment);
        size_t overhead     = candidate - this->cached_cursor->end();

        // Check if the gap after the cursor is big enough and ensure we don't
        // wrap around or exceed user space
        if (this->cached_cursor->gap >= (size + overhead) && (candidate + size <= USER_END)) {
            return candidate;
        }
    }

    uintptr_t found = this->find_hole(this->root, size, alignment);
    if (found) {
        return found;
    }

    UserVmRegion* max = this->root;

    if (!max) {
        return align_up(USER_START, alignment);
    }

    while (max->right) {
        max = max->right;
    }

    uintptr_t tail = align_up(max->end(), alignment);
    if (tail + size <= USER_END) {
        return tail;
    }

    return 0;
}

uintptr_t UserAddressSpace::find_hole(UserVmRegion* node, size_t size, size_t alignment) {
    if (!node) {
        return 0;
    }

    if (node->subtree_max_gap < size) {
        return 0;
    }

    if (node->left && node->left->subtree_max_gap >= size) {
        uintptr_t res = this->find_hole(node->left, size, alignment);

        if (res) {
            return res;
        }
    }

    uintptr_t candidate   = align_up(node->end(), alignment);
    size_t align_overhead = candidate - node->end();

    // Ensure that the gap is physically large enoguht to hold (size + alignment adjustment)
    if (node->gap >= (size + align_overhead)) {
        if (candidate + size <= USER_END) {
            return candidate;
        }
    }

    // Try right subtree.
    if (node->right && node->right->subtree_max_gap >= size) {
        return this->find_hole(node->right, size, alignment);
    }

    return 0;
}

void UserAddressSpace::update_node_metadata(UserVmRegion* x) {
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

void UserAddressSpace::insert_region(uintptr_t start, size_t size, uint8_t flags, CacheType cache,
                                     PageSize type) {
    UserVmRegion* z = this->metadata_allocator.allocate();
    z->start        = start;
    z->size         = size;
    z->flags        = flags;
    z->cache        = cache;
    z->page_size    = type;
    z->is_red       = true;
    z->left = z->right = nullptr;
    z->parent          = nullptr;

    UserVmRegion* y = nullptr;
    UserVmRegion* x = this->root;

    while (x != nullptr) {
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
    UserVmRegion* pred = this->predecessor(z);
    UserVmRegion* succ = this->successor(z);

    if (succ) {
        // Distance to successor
        z->gap = succ->start - z->end();
    } else {
        // Z is the last node in memory. Gap is effectively infinite
        z->gap = USER_END - z->end();
    }

    // Distance to Z
    if (pred) {
        pred->gap = z->start - pred->end();
        this->update_path_to_root(pred);
    }

    this->update_path_to_root(z);
    this->cached_cursor = z;
    this->insert_fixup(z);
}

void UserAddressSpace::delete_node(UserVmRegion* z) {
    // Since we are about to remove the logical range z. We must
    // merge z's gap into its predessor's gap.
    // Now new Gap for Pred = (gap before z) + (z's size) + (gap after z)
    UserVmRegion* pred = this->predecessor(z);
    if (pred) {
        pred->gap += z->size + z->gap;
    }

    UserVmRegion *x, *y;
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
    UserVmRegion* update_start = y->parent;

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

void UserAddressSpace::update_path_to_root(UserVmRegion* x) {
    while (x) {
        this->update_node_metadata(x);
        x = x->parent;
    }
}

UserVmRegion* UserAddressSpace::find_region_containing(uintptr_t addr) {
    UserVmRegion* curr = this->root;

    while (curr) {
        if (addr >= curr->start && addr < curr->end()) {
            return curr;
        }

        if (addr < curr->start) {
            curr = curr->left;
        } else {
            curr = curr->right;
        }
    }

    return nullptr;
}

bool UserAddressSpace::check_overlap(uintptr_t start, size_t size) {
    uintptr_t end      = start + size;
    UserVmRegion* curr = this->root;

    while (curr) {
        if (start < curr->end() && end > curr->start) {
            return true;
        }

        if (start < curr->start) {
            curr = curr->left;
        } else {
            curr = curr->right;
        }
    }

    return false;
}

UserVmRegion* UserAddressSpace::predecessor(UserVmRegion* node) {
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
    UserVmRegion* p = node->parent;
    while (p && node == p->left) {
        node = p;
        p    = p->parent;
    }

    return p;
}

UserVmRegion* UserAddressSpace::successor(UserVmRegion* node) {
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
    UserVmRegion* p = node->parent;
    while (p && node == p->right) {
        node = p;
        p    = p->parent;
    }

    return p;
}

void UserAddressSpace::rotate_left(UserVmRegion* x) {
    UserVmRegion* y = x->right;
    x->right        = y->left;

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

void UserAddressSpace::rotate_right(UserVmRegion* x) {
    UserVmRegion* y = x->left;
    x->left         = y->right;

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

void UserAddressSpace::insert_fixup(UserVmRegion* z) {
    while (z->parent && z->parent->is_red) {
        if (z->parent == z->parent->parent->left) {
            UserVmRegion* y = z->parent->parent->right;

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
            UserVmRegion* y = z->parent->parent->left;

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

void UserAddressSpace::delete_fixup(UserVmRegion* x) {
    while (x != root && !x->is_red) {
        if (x == x->parent->left) {
            UserVmRegion* w = x->parent->right;

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
            UserVmRegion* w = x->parent->left;

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
}  // namespace kernel::memory