#include "memory/heap.hpp"
#include "boot/boot.h"
#include "hal/smp_manager.hpp"
#include "libs/log.hpp"
#include "memory/vmm.hpp"
#include "libs/math.hpp"
#include <string.h>

namespace kernel::memory {
HeapMap::Node* HeapMap::root = nullptr;
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

    if (!root) {
        root = reinterpret_cast<Node*>(VirtualManager::allocate(1));
        if (!root) {
            PANIC("Out of Memory!");
        }
    }

    Node* l4 = root;

    // Level 4 -> Level 3
    uintptr_t i4 = (addr >> 39) & MASK;
    if (!l4->entries[i4]) {
        l4->entries[i4] = VirtualManager::allocate(1);
    }

    Node* l3 = reinterpret_cast<Node*>(l4->entries[i4]);

    // Level 3 -> Level 2
    uintptr_t i3 = (addr >> 30) & MASK;
    if (!l3->entries[i3]) {
        l3->entries[i3] = VirtualManager::allocate(1);
    }

    Node* l2 = reinterpret_cast<Node*>(l3->entries[i3]);

    // Level 2 -> Level 1
    uintptr_t i2 = (addr >> 21) & MASK;
    if (!l2->entries[i2]) {
        l2->entries[i2] = VirtualManager::allocate(1);
    }

    Node* l1        = reinterpret_cast<Node*>(l2->entries[i2]);
    uintptr_t i1    = (addr >> 12) & MASK;
    l1->entries[i1] = reinterpret_cast<void*>(meta);
}

Slab* HeapMap::get(void* ptr) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);

    if (!root) {
        return nullptr;
    }

    Node* l4 = root;
    Node* l3 = reinterpret_cast<Node*>(l4->entries[(addr >> 39) & MASK]);

    if (!l3) {
        return nullptr;
    }

    Node* l2 = reinterpret_cast<Node*>(l3->entries[(addr >> 30) & MASK]);

    if (!l2) {
        return nullptr;
    }

    Node* l1 = reinterpret_cast<Node*>(l2->entries[(addr >> 21) & MASK]);

    if (!l1) {
        return nullptr;
    }

    return reinterpret_cast<Slab*>(l1->entries[(addr >> 12) & MASK]);
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

void SlubAllocator::free(void* ptr) {
    if (!ptr) {
        return;
    }

    Slab* s = HeapMap::get(ptr);

    if (!s) {
        PANIC("Out of Memory");
        return;
    }

    if (s->is_large) {
        this->free_large(s, ptr);
        return;
    }

    LockGuard guard(this->irq_lock);
    uint32_t cpu_id = 0;

    if (cpu::CpuCoreManager::get().initialized()) {
        cpu_id = cpu::CpuCoreManager::get().get_current_core()->core_idx;
    }

    auto& cache = this->cpu_caches[cpu_id].classes[s->size_class];

    if (cache.free_count < FREE_BATCH_SIZE) {
        cache.free_buf[cache.free_count++] = ptr;
    } else {
        this->flush(s->size_class, cache);
        cache.free_buf[cache.free_count++] = ptr;
    }
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
        VirtualManager::free(page, 1);
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

    // Process all pointers in the batch
    for (int i = 0; i < cache.free_count; ++i) {
        void* ptr = cache.free_buf[i];
        Slab* s   = HeapMap::get(ptr);

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
        VirtualManager::free(ptr, pages);
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
    VirtualManager::free(ptr, pages);
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