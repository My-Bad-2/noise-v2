#pragma once

#include "libs/spinlock.hpp"
#include "memory/memory.hpp"
#include <bit>

// 48 bits total. 12 bits -> offset. 36 bits index
// Similar to how x86_64's paging behaves.
// Level 4: Bits 39-47
// Level 3: Bits 30-38
// Level 2: Bits 21-29
// Level 1: Bits 12-20
#define MASK            0x1ff
#define FREE_BATCH_SIZE 32

namespace kernel::memory {
struct alignas(32) Slab {
    void* freelist;
    Slab* next;
    Slab* prev;
    void* page_addr;

    uint16_t in_use;
    uint16_t total;
    uint16_t size_class;
    uint16_t is_large;
};

class MetadataAllocator {
   public:
    Slab* alloc();
    void free(Slab* s);

    static MetadataAllocator& get();

   private:
    struct Page {
        Page* next;
        uint8_t data[memory::PAGE_SIZE_4K - sizeof(Page*)];
    };

    Page* head      = nullptr;
    size_t offset   = 0;
    Slab* free_pool = nullptr;
    SpinLock lock;
};

// Inspiration: Radix maps used by x86_64 architecture for mapping
// Virtual Address Space to Physical Address Space.
class HeapMap {
   public:
    static void set(void* ptr, Slab* meta);
    static Slab* get(void* ptr);

    struct Node {
        // Can be Node* (Levels 4-2)
        std::atomic<void*> entries[MASK + 1];
    };

   private:
    static std::atomic<Node*> root;
    static SpinLock lock;
};

struct HeapTLB {
    static constexpr size_t TLB_SIZE = 64;
    static constexpr size_t TLB_MASK = TLB_SIZE - 1;

    struct Entry {
        uintptr_t page_base;
        Slab* slab;
    } entries[TLB_SIZE];

    void init();

    Slab* lookup(void* ptr);
    void insert(void* ptr, Slab* s);
};

class SlubAllocator {
   public:
    SlubAllocator();

    void init();
    void* allocate(size_t size);
    void free(void* ptr);

    static SlubAllocator& get();

   private:
    inline int get_size_idx(size_t size) const {
        if (size > 2048) {
            return -1;
        }

        if (size <= 16) {
            return 0;
        } else {
            // Log 2
            return std::bit_width(size - 1) - 4;
        }
    }

    void* take_object(Slab* s);
    Slab* refill_slab(int idx);

    static inline void list_add(Slab*& head, Slab* s) {
        s->next = head;
        s->prev = nullptr;

        if (head) {
            head->prev = s;
        }

        head = s;
    }

    static inline void list_remove(Slab*& head, Slab* s) {
        if (s->prev) {
            s->prev->next = s->next;
        } else {
            head = s->next;
        }

        if (s->next) {
            s->next->prev = s->prev;
        }

        s->next = s->prev = nullptr;
    }

    void* alloc_large(size_t size);
    void free_large(Slab* s, void* ptr);

    // 16, 32, 64, 128, 256, 512, 1K, 2K
    static constexpr int NUM_CLASSES = 8;

    struct SizeClass {
        size_t size;
        Slab* partial;
        Slab* empty;
        SpinLock lock;
    };

    struct alignas(CACHE_LINE_SIZE) CpuCache {
        HeapTLB tlb;

        struct ClassCache {
            Slab* active;
            void* free_buf[FREE_BATCH_SIZE];
            int free_count;
        } classes[NUM_CLASSES];
    };

    void flush(int idx, CpuCache::ClassCache& cache);

    SizeClass size_classes[NUM_CLASSES];
    CpuCache* cpu_caches = nullptr;
    size_t num_cpus      = 0;
    bool initialized     = false;

    InterruptLock irq_lock;
};

void* kmalloc(size_t size);
void kfree(void* ptr);
void* aligned_kalloc(size_t size, size_t alignment);
void aligned_kfree(void* ptr);
}  // namespace kernel::memory