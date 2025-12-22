#include "memory/heap.hpp"
#include "boot/boot.h"
#include "hal/smp_manager.hpp"
#include "libs/log.hpp"
#include "memory/memory.hpp"
#include "memory/vmm.hpp"
#include "libs/math.hpp"
#include <string.h>
#include <atomic>

namespace kernel::memory {
std::atomic<HeapMap::Node*> HeapMap::root = nullptr;
SpinLock HeapMap::lock;

Slab* MetadataAllocator::alloc() {
    LockGuard guard(this->lock);

    // If we have a free slab available, use it
    if (this->free_pool) {
        Slab* s         = this->free_pool;
        this->free_pool = s->next;

        return new (s) Slab();
    }

    if (!this->head || (offset + sizeof(Slab) > sizeof(Page::data))) {
        // need a new page
        void* ptr = VirtualManager::allocate(1);

        if (!ptr) {
            return nullptr;
        }

        Page* pg     = reinterpret_cast<Page*>(ptr);
        pg->next     = this->head;
        this->head   = pg;
        this->offset = 0;
    }

    Slab* s = reinterpret_cast<Slab*>(&this->head->data[offset]);
    this->offset += sizeof(Slab);
    return new (s) Slab();
}

void MetadataAllocator::free(Slab* s) {
    LockGuard guard(this->lock);
    s->next         = this->free_pool;
    this->free_pool = s;
}

MetadataAllocator& MetadataAllocator::get() {
    static MetadataAllocator allocator;
    return allocator;
}

void HeapMap::set(void* ptr, Slab* meta) {
    LockGuard guard(lock);
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);

    Node* l4 = root.load(std::memory_order_relaxed);

    if (!l4) {
        l4 = reinterpret_cast<Node*>(VirtualManager::allocate(1));
        memset(l4, 0, PAGE_SIZE_4K);

        root.store(l4, std::memory_order_release);
    }

    // Level 4 -> Level 3
    uintptr_t i4 = (addr >> 39) & MASK;
    Node* l3     = static_cast<Node*>(l4->entries[i4].load(std::memory_order_relaxed));
    if (!l3) {
        l3 = reinterpret_cast<Node*>(VirtualManager::allocate(1));
        memset(l3, 0, PAGE_SIZE_4K);

        l4->entries[i4].store(l3, std::memory_order_release);
    }

    // Level 3 -> Level 2
    uintptr_t i3 = (addr >> 30) & MASK;
    Node* l2     = static_cast<Node*>(l3->entries[i3].load(std::memory_order_relaxed));
    if (!l2) {
        l2 = reinterpret_cast<Node*>(VirtualManager::allocate(1));
        memset(l2, 0, PAGE_SIZE_4K);

        l3->entries[i3].store(l2, std::memory_order_release);
    }

    // Level 2 -> Level 1
    uintptr_t i2 = (addr >> 21) & MASK;
    Node* l1     = static_cast<Node*>(l2->entries[i2].load(std::memory_order_relaxed));
    if (!l1) {
        l1 = reinterpret_cast<Node*>(VirtualManager::allocate(1));
        memset(l1, 0, PAGE_SIZE_4K);

        l2->entries[i2].store(l1, std::memory_order_release);
    }

    uintptr_t i1 = (addr >> 12) & MASK;
    l1->entries[i1].store(meta, std::memory_order_release);
}

Slab* HeapMap::get(void* ptr) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);

    Node* l4 = root.load(std::memory_order_acquire);
    if (!l4) {
        return nullptr;
    }

    uintptr_t i4 = (addr >> 39) & MASK;
    Node* l3     = static_cast<Node*>(l4->entries[i4].load(std::memory_order_acquire));
    if (!l3) {
        return nullptr;
    }

    uintptr_t i3 = (addr >> 30) & MASK;
    Node* l2     = static_cast<Node*>(l3->entries[i3].load(std::memory_order_acquire));
    if (!l2) {
        return nullptr;
    }

    uintptr_t i2 = (addr >> 21) & MASK;
    Node* l1     = static_cast<Node*>(l2->entries[i2].load(std::memory_order_acquire));
    if (!l1) {
        return nullptr;
    }

    uintptr_t i1 = (addr >> 12) & MASK;
    return static_cast<Slab*>(l1->entries[i1].load(std::memory_order_acquire));
}

void HeapTLB::init() {
    memset(this->entries, 0, sizeof(this->entries));
}

Slab* HeapTLB::lookup(void* ptr) {
    uintptr_t addr      = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t page_base = align_down(addr, PAGE_SIZE_4K);

    // Use bits 12-18 of the address as mask
    size_t idx = (page_base >> 12) & TLB_MASK;

    if (this->entries[idx].page_base == page_base) {
        // TLB Hit!
        return this->entries[idx].slab;
    }

    return nullptr;
}

void HeapTLB::insert(void* ptr, Slab* s) {
    uintptr_t addr      = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t page_base = align_down(addr, PAGE_SIZE_4K);

    size_t idx = (page_base >> 12) & TLB_MASK;

    this->entries[idx].page_base = page_base;
    this->entries[idx].slab      = s;
}

SlubAllocator::SlubAllocator() {
    size_t s = 16;

    for (int i = 0; i < NUM_CLASSES; ++i) {
        this->size_classes[i].size    = s;
        this->size_classes[i].partial = nullptr;
        this->size_classes[i].empty   = nullptr;

        s *= 2;
    }
}

void SlubAllocator::init() {
    if (this->initialized) {
        return;
    }

    this->num_cpus   = mp_request.response->cpu_count;
    size_t bytes     = sizeof(CpuCache) * this->num_cpus;
    size_t pages     = div_roundup(bytes, PAGE_SIZE_4K);
    this->cpu_caches = reinterpret_cast<CpuCache*>(VirtualManager::allocate(pages));

    memset(this->cpu_caches, 0, pages * PAGE_SIZE_4K);

    this->initialized = true;
}

void* SlubAllocator::allocate(size_t size) {
    if (!this->initialized) {
        return nullptr;
    }

    int idx = this->get_size_idx(size);
    if (idx == -1) {
        return this->alloc_large(size);
    }

    LockGuard guard(this->irq_lock);

    uint32_t cpu_id = 0;

    if (cpu::CpuCoreManager::get().initialized()) {
        cpu_id = cpu::CpuCoreManager::get().get_current_core()->core_idx;
    }

    auto& cache = this->cpu_caches[cpu_id].classes[idx];

    if (cache.active && cache.active->freelist) {
        return this->take_object(cache.active);
    }

    Slab* new_slab = this->refill_slab(idx);

    if (new_slab) {
        cache.active = new_slab;
        return this->take_object(new_slab);
    }

    return nullptr;
}

void SlubAllocator::free_slow(void* ptr, CpuCache& cache) {
    Slab* s = cache.tlb.lookup(ptr);

    if (!s) {
        uintptr_t page_base = align_down(reinterpret_cast<uintptr_t>(ptr), PAGE_SIZE_4K);

        // Check the 8 active slabs currently loaded in L1 cache
        for (int i = 0; i < NUM_CLASSES; ++i) {
            Slab* active = cache.classes[i].active;

            if (active && (reinterpret_cast<uintptr_t>(active->page_addr) == page_base)) {
                s = active;

                // Update the TLB
                cache.tlb.insert(ptr, s);
                break;
            }
        }

        // Check the radix tree
        if (!s) {
            s = HeapMap::get(ptr);

            if (unlikely(!s)) {
                PANIC("Double free or invalid pointer!");
                return;
            }

            cache.tlb.insert(ptr, s);
        }
    }

    if (unlikely(s->is_large)) {
        this->free_large(s, ptr);
        return;
    }

    auto& class_cache = cache.classes[s->size_class];

    if (unlikely(class_cache.free_count >= FREE_BATCH_SIZE)) {
        this->flush(s->size_class, class_cache);
    }

    class_cache.free_buf[class_cache.free_count++] = ptr;
}

void SlubAllocator::free(void* ptr) {
    if (unlikely(!ptr)) {
        return;
    }

    LockGuard guard(this->irq_lock);

    uint32_t cpu_id = 0;

    if (cpu::CpuCoreManager::get().initialized()) {
        cpu_id = cpu::CpuCoreManager::get().get_current_core()->core_idx;
    }

    auto& cache = this->cpu_caches[cpu_id];

    Slab* s = cache.tlb.lookup(ptr);

    if (likely(s)) {
        auto& class_cache = cache.classes[s->size_class];

        if (likely(class_cache.free_count < FREE_BATCH_SIZE)) {
            class_cache.free_buf[class_cache.free_count++] = ptr;
            return;
        }
    }

    // Could be anything: TLB Miss, Buffer Full, Large Page
    this->free_slow(ptr, cache);
}

void* SlubAllocator::take_object(Slab* s) {
    void* obj = s->freelist;
    // Read embedded next pointer
    s->freelist = *reinterpret_cast<void**>(obj);
    s->in_use++;
    return obj;
}

Slab* SlubAllocator::refill_slab(int idx) {
    SizeClass& sc = this->size_classes[idx];
    LockGuard guard(sc.lock);

    if (sc.partial) {
        Slab* s = sc.partial;
        this->list_remove(sc.partial, s);
        s->next = s->prev = nullptr;
        return s;
    }

    if (sc.empty) {
        Slab* s = sc.empty;
        this->list_remove(sc.empty, s);
        return s;
    }

    void* page = VirtualManager::allocate(1);

    if (!page) {
        return nullptr;
    }

    Slab* s = MetadataAllocator::get().alloc();

    if (!s) {
        VirtualManager::free(page);
        return nullptr;
    }

    s->page_addr  = page;
    s->size_class = static_cast<uint16_t>(idx);
    s->total      = PAGE_SIZE_4K / sc.size;
    s->in_use     = 0;
    s->is_large   = 0;

    char* base = reinterpret_cast<char*>(page);

    // Thread the free list
    for (size_t i = 0; i < s->total - 1; ++i) {
        *reinterpret_cast<void**>(base + i * sc.size) = (base + (i + 1) * sc.size);
    }

    *reinterpret_cast<void**>(base + (s->total - 1) * sc.size) = nullptr;
    s->freelist                                                = base;

    HeapMap::set(page, s);
    return s;
}

void SlubAllocator::flush(int idx, CpuCache::ClassCache& cache) {
    SizeClass& sc = this->size_classes[idx];
    LockGuard guard(sc.lock);

    Slab* last_slab          = nullptr;
    uintptr_t last_page_base = 0;

    // Process all pointers in the batch
    for (int i = 0; i < cache.free_count; ++i) {
        void* ptr = cache.free_buf[i];

        if ((i + 1) < cache.free_count) {
            // 0 -> Read
            // 3 - > High temporal locality
            __builtin_prefetch(cache.free_buf[i + 1], 0, 3);
        }

        Slab* s;
        uintptr_t curr_page_base = align_down(reinterpret_cast<uintptr_t>(ptr), PAGE_SIZE_4K);

        if (curr_page_base == last_page_base) {
            s = last_slab;
        } else {
            s              = HeapMap::get(ptr);
            last_slab      = s;
            last_page_base = curr_page_base;
        }

        // Return to freelist
        *reinterpret_cast<void**>(ptr) = s->freelist;
        s->freelist                    = ptr;
        s->in_use--;

        if (s->in_use == s->total - 1) {
            // Transition: Full -> Partial
            this->list_add(sc.partial, s);
        } else if (s->in_use == 0) {
            // Transition: Partial -> Empty
            this->list_remove(sc.partial, s);
            this->list_add(sc.empty, s);
        }
        // If it remains partial -> do nothing
    }

    cache.free_count = 0;
}

void* SlubAllocator::alloc_large(size_t size) {
    size_t pages = div_roundup(size, PAGE_SIZE_4K);
    void* ptr    = VirtualManager::allocate(pages);

    if (!ptr) {
        return nullptr;
    }

    Slab* s = MetadataAllocator::get().alloc();

    if (!s) {
        VirtualManager::free(ptr);
        return nullptr;
    }

    s->page_addr = ptr;
    s->is_large  = 1;
    s->in_use    = static_cast<uint16_t>(pages);

    HeapMap::set(ptr, s);
    return ptr;
}

void SlubAllocator::free_large(Slab* s, void* ptr) {
    size_t pages = s->in_use;
    HeapMap::set(ptr, nullptr);
    VirtualManager::free(ptr);
    MetadataAllocator::get().free(s);
}

SlubAllocator& SlubAllocator::get() {
    static SlubAllocator allocator;
    return allocator;
}

void kfree(void* ptr) {
    SlubAllocator& slub = SlubAllocator::get();
    slub.free(ptr);
}

void* kmalloc(size_t size) {
    SlubAllocator& slub = SlubAllocator::get();
    return slub.allocate(size);
}

void* aligned_kalloc(size_t size, size_t alignment) {
    if (alignment == 0 || !is_aligned(alignment, alignment)) {
        return nullptr;
    }

    size_t overhead = alignment + sizeof(void*);

    if (size > SIZE_MAX - overhead) {
        return nullptr;
    }

    SlubAllocator& slub = SlubAllocator::get();
    void* raw_ptr       = slub.allocate(size + overhead);
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

    SlubAllocator& slub = SlubAllocator::get();

    uintptr_t addr        = reinterpret_cast<uintptr_t>(ptr);
    void** stash_location = reinterpret_cast<void**>(addr - sizeof(void*));
    void* raw_ptr         = *stash_location;

    slub.free(raw_ptr);
}
}  // namespace kernel::memory