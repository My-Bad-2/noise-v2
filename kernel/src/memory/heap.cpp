#include "libs/log.hpp"
#include "memory/vmm.hpp"
#include "memory/heap.hpp"
#include "libs/math.hpp"

#define SLAB_MIN_SIZE 16
#define SLAB_MAX_SIZE 2048

#define CACHE_LINE_SIZE 64

#define SLAB_MAGIC       0x51AB51ABu
#define LARGE_SLAB_MAGIC 0xB166B166u

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

// NOLINTBEGIN(performance-no-int-to-ptr)
namespace kernel::memory {
// NOLINTNEXTLINE
void SlabHeader::init(size_t obj_size, size_t color) {
    this->magic        = SLAB_MAGIC;
    this->object_size  = static_cast<uint16_t>(obj_size);
    this->used_count   = 0;
    this->color_offset = static_cast<uint16_t>(color);
    this->next         = nullptr;
    this->prev         = nullptr;

    uintptr_t base  = reinterpret_cast<uintptr_t>(this);
    uintptr_t start = align_up(base + sizeof(SlabHeader), 16u);

    start += color;

    if (start >= base + PAGE_SIZE_4K) {
        start = base + sizeof(SlabHeader);
    }

    uintptr_t end = base + PAGE_SIZE_4K;
    total_objects = static_cast<uint16_t>((end - start) / obj_size);

    this->free_list = reinterpret_cast<void*>(start);
    uintptr_t curr  = start;

    for (uint16_t i = 0; i < this->total_objects - 1; ++i) {
        uintptr_t next_node             = curr + obj_size;
        *reinterpret_cast<void**>(curr) = reinterpret_cast<void*>(next_node);
        curr                            = next_node;
    }

    *reinterpret_cast<void**>(curr) = nullptr;

    // The "color" offset is used to shift where objects begin within
    // a slab so that frequently-accessed fields don't always map to
    // the same cache lines across slabs, reducing hot-spot contention.
}

void SlabCache::init(size_t size) {
    // Initialize one size-class cache. We choose coloring parameters and
    // free-slab limits here to trade off between memory and locality.
    this->obj_size       = size;
    this->curr_color     = 0;
    this->max_color      = 0;   // set appropriately in your code
    this->free_count     = 0;
    this->max_free_slabs = 8;   // example policy

    if (size <= 64) {
        this->max_free_slabs = 16;
    } else if (size >= 1024) {
        this->max_free_slabs = 2;
    } else {
        this->max_free_slabs = 5;
    }

    LOG_INFO("SlabCache: init size=%zu", size);
}

void* SlabCache::alloc() {
    LockGuard guard(this->lock);

    // Prefer partially used slabs first to keep the working set compact.
    if (likely(this->partial_head != nullptr)) {
        return this->alloc_from_slab(this->partial_head);
    }

    if (this->free_head != nullptr) {
        SlabHeader* slab = this->free_head;
        this->remove_free_node(slab);
        this->push(this->partial_head, slab);
        return this->alloc_from_slab(slab);
    }

    return this->grow_and_alloc();
}

void SlabCache::free(SlabHeader* slab, void* ptr) {
    LockGuard guard(this->lock);

    // Return object to slab freelist; adjust slab state (partial/full/free).
    // This classification lets us quickly find good candidates for reuse
    // and reclaim fully free slabs later.
    *reinterpret_cast<void**>(ptr) = slab->free_list;
    slab->free_list                = ptr;
    slab->used_count--;

    if (slab->used_count == slab->total_objects - 1) {
        this->unlink(this->full_head, slab);
        this->push(this->partial_head, slab);
    } else if (slab->used_count == 0) {
        this->unlink(this->partial_head, slab);
        this->push_free_node(slab);

        if (this->free_count > this->max_free_slabs) {
            if (free_tail) {
                SlabHeader* victim = this->free_tail;
                this->remove_free_node(victim);
                VirtualManager::free(victim, 1);
            }
        }
    }

    LOG_DEBUG("SlabCache: free ptr=%p size=%zu used=%u/%u",
              ptr, this->obj_size, slab->used_count, slab->total_objects);
}

void SlabCache::reap() {
    LockGuard guard(this->lock);

    // Reap is a background hygiene mechanism: if we have accumulated
    // more completely free slabs than `max_free_slabs`, we release
    // some of them back to the VMM to keep memory footprint bounded.
    while (this->free_head) {
        SlabHeader* slab = this->free_head;
        this->unlink(this->free_head, slab);
        VirtualManager::free(slab, 1);
    }

    LOG_INFO("SlabCache: reap for size=%zu free_slabs=%zu", this->obj_size, this->free_count);
}

void* SlabCache::alloc_from_slab(SlabHeader* slab) {
    // Fast path: consume an object from the slab's freelist.
    void* ptr       = slab->free_list;
    slab->free_list = *reinterpret_cast<void**>(ptr);
    slab->used_count++;

    if (unlikely(slab->free_list == nullptr)) {
        this->unlink(this->partial_head, slab);
        this->push(full_head, slab);
    }

    return ptr;
}

void* SlabCache::grow_and_alloc() {
    // Slow path: allocate a new slab (typically one or more pages) from
    // the VMM, carve it into objects, and then allocate from it.
    void* page = VirtualManager::allocate(1);

    if (page == nullptr) {
        return nullptr;
    }

    SlabHeader* slab = reinterpret_cast<SlabHeader*>(page);

    size_t color     = this->curr_color;
    this->curr_color = (this->curr_color + 16) % this->max_color;

    slab->init(this->obj_size, color);
    this->push(this->partial_head, slab);

    LOG_DEBUG("SlabCache: grew new slab for obj_size=%zu", this->obj_size);

    return alloc_from_slab(slab);
}

void SlabCache::push_free_node(SlabHeader* node) {
    node->next = this->free_head;
    node->prev = nullptr;

    if (this->free_head != nullptr) {
        this->free_head->prev = node;
    } else {
        this->free_tail = node;
    }

    this->free_head = node;
    this->free_count++;
}

void SlabCache::remove_free_node(SlabHeader* node) {
    if (node->prev != nullptr) {
        node->prev->next = node->next;
    } else {
        this->free_head = node->next;
    }

    if (node->next != nullptr) {
        node->next->prev = node->prev;
    } else {
        this->free_tail = node->prev;
    }

    node->next = nullptr;
    node->prev = nullptr;
    this->free_count--;
}

void SlabCache::unlink(SlabHeader*& head, SlabHeader* node) {
    if (node->prev != nullptr) {
        node->prev->next = node->next;
    } else {
        head = node->next;
    }

    if (node->next != nullptr) {
        node->next->prev = node->prev;
    }

    node->next = node->prev = nullptr;
}

void SlabCache::push(SlabHeader*& head, SlabHeader* node) {
    node->next = head;
    node->prev = nullptr;

    if (head) {
        head->prev = node;
    }

    head = node;
}

int KernelHeap::get_index(size_t size) {
    if (size <= SLAB_MIN_SIZE) {
        return 0;
    }

    if (size > SLAB_MAX_SIZE) {
        return -1;
    }

    return static_cast<int>((sizeof(size_t) * 8) - 1) - __builtin_ctzll(size);
}

void KernelHeap::init() {
    // Initialize slab caches for a set of power-of-two (or tuned) sizes.
    // The choice of classes reflects a compromise between internal
    // fragmentation and metadata overhead.
    size_t size = SLAB_MIN_SIZE;

    for (int i = 0; i < 12; ++i) {
        this->caches[i].init(size);
        size <<= 1;
    }

    LOG_INFO("KernelHeap: initialized slab caches");
}

void* KernelHeap::malloc(size_t size) {
    if (size == 0) {
        return nullptr;
    }

    // Decide between slab allocation and large allocation based on size.
    // Small sizes are rounded to a cache class; very large sizes bypass
    // the slab layer entirely.
    int idx = this->get_index(size);
    if (likely(idx >= 0)) {
        return this->caches[idx].alloc();
    }

    return this->alloc_large(size);
}

void KernelHeap::free(void* ptr) {
    if (!ptr) {
        return;
    }

    // Distinguish between slab-managed and large allocations. Typically
    // this is done by looking at a magic/header just before the pointer
    // or by classifying address ranges.
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);

    BigAllocHeader* big_hdr = reinterpret_cast<BigAllocHeader*>(addr - sizeof(BigAllocHeader));
    if (big_hdr->magic == LARGE_SLAB_MAGIC) {
        this->free_large(big_hdr);
        return;
    }

    uintptr_t page_start = align_down(addr, PAGE_SIZE_4K);
    SlabHeader* slab     = reinterpret_cast<SlabHeader*>(page_start);

    if (likely(slab->magic == SLAB_MAGIC)) {
        int idx = this->get_index(slab->object_size);
        this->caches[idx].free(slab, ptr);
    } else {
        PANIC("Pointer Corruption/Invalid Free");
    }

    LOG_DEBUG("KernelHeap: free %p", ptr);
}

void* KernelHeap::alloc_large(size_t size) {
    // For big requests, fall back to page-granularity allocations via VMM.
    // This avoids polluting small-object slabs with huge chunks.
    size_t required_size = size + sizeof(BigAllocHeader);

    PageSize size_type = PageSize::Size4K;
    size_t page_size   = PAGE_SIZE_4K;
    size_t count       = 0;

    if (required_size >= PAGE_SIZE_1G) {
        size_type = PageSize::Size1G;
        page_size = PAGE_SIZE_1G;
    } else if (required_size >= PAGE_SIZE_2M / 2) {
        size_type = PageSize::Size2M;
        page_size = PAGE_SIZE_2M;
    } else {
        size_type = PageSize::Size4K;
        page_size = PAGE_SIZE_4K;
    }

    count = (required_size + page_size - 1) / page_size;

    void* raw_mem = VirtualManager::allocate(count, size_type);

    // If 1GB/2MB alloc fails, downgrade to 4KB pages
    if ((raw_mem == nullptr) || (size_type != PageSize::Size4K)) {
        size_type = PageSize::Size4K;
        page_size = PAGE_SIZE_4K;

        count = (required_size + page_size - 1) / page_size;

        raw_mem = VirtualManager::allocate(count);
    }

    void* user_ptr = nullptr;
    if (raw_mem) {
        BigAllocHeader* hdr = static_cast<BigAllocHeader*>(raw_mem);
        hdr->magic = LARGE_SLAB_MAGIC;
        hdr->page_count = static_cast<uint32_t>(count);
        hdr->size_type = size_type;

        user_ptr = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(hdr) + sizeof(BigAllocHeader));
    }

    if (!user_ptr) {
        LOG_ERROR("KernelHeap: alloc_large failed size=%zu", size);
    } else {
        LOG_DEBUG("KernelHeap: alloc_large ptr=%p size=%zu", user_ptr, size);
    }
    
    return user_ptr;
}

void KernelHeap::free_large(BigAllocHeader* hdr) {
    void* raw_mem = static_cast<void*>(hdr);

    size_t count = hdr->page_count;
    PageSize type = hdr->size_type;

    VirtualManager::free(raw_mem, count, type);

    LOG_DEBUG("KernelHeap: free_large pages=%u type=%u", hdr->page_count,
              static_cast<unsigned>(hdr->size_type));
}

KernelHeap& KernelHeap::instance() {
    static KernelHeap heap;
    return heap;
}
}  // namespace kernel::memory
// NOLINTEND(performance-no-int-to-ptr)