#pragma once

#include "libs/mutex.hpp"
#include "libs/spinlock.hpp"
#include "memory/pagemap.hpp"

namespace kernel::task {
struct Process;
}

namespace kernel::memory {
struct VmRegion {
    uintptr_t start;
    size_t size;

    size_t gap;
    size_t subtree_max_gap;

    uint8_t flags;
    CacheType cache;

    VmRegion* parent = nullptr;
    VmRegion* left   = nullptr;
    VmRegion* right  = nullptr;
    bool is_red      = true;

    uintptr_t end() const {
        return this->start + this->size;
    }
};

class VmRegionAllocator {
   public:
    VmRegion* allocate();
    void deallocate(VmRegion* node);

   private:
    void refill();

    struct FreeNode {
        FreeNode* next;
    };

    FreeNode* free_head = nullptr;
    SpinLock lock;
};

class VirtualMemoryAllocator {
   public:
    void init(uintptr_t start_addr);

    void* allocate(size_t size, uint8_t flags = Read | Write,
                   CacheType cache = CacheType::WriteBack);

    void* reserve(size_t size, size_t alignment, uint8_t flags);
    void free(void* ptr, bool free_phys);

   private:
    void map(uintptr_t virt_addr, size_t size, uint8_t flags, CacheType cache);
    void unmap(uintptr_t virt_addr, size_t size, bool free_phys);

    VmRegion* find_node(uintptr_t start);
    uintptr_t find_hole(size_t size, size_t alignment);
    uintptr_t find_hole(VmRegion* node, size_t size, size_t alignment);

    void insert_region(uintptr_t start, size_t size, uint8_t flags, CacheType cache);
    void insert_region_locked(uintptr_t start, size_t size, uint8_t flags, CacheType cache);

    void delete_node_locked(VmRegion* z);
    void rotate_left(VmRegion* x);
    void rotate_right(VmRegion* x);
    void insert_fixup(VmRegion* z);
    void delete_fixup(VmRegion* x);

    void update_node_metadata(VmRegion* x);
    void update_path_to_root(VmRegion* x);

    VmRegion* predecessor(VmRegion* node);
    VmRegion* successor(VmRegion* node);

    struct alignas(CACHE_LINE_SIZE) CpuCache {
        static constexpr int CAPACITY = 256;
        uintptr_t va_holes[CAPACITY];
        int count = 0;
        IrqLock lock;
    };

    uintptr_t heap_base;
    size_t cpu_count;

    CpuCache* caches = nullptr;

    SpinLock lock;
    VmRegion* root;
    VmRegion* cached_cursor = nullptr;
    VmRegionAllocator metadata_allocator;
};

class UserAddressSpace {
   public:
    static constexpr uintptr_t USER_START = 0x1000;
    static constexpr uintptr_t USER_END   = 0x00007FFFFFFFFFFF;

    static void arch_init();

    void init(task::Process* proc);
    ~UserAddressSpace();

    void* allocate(size_t size, uint8_t flags = Read | Write);
    bool allocate_specific(uintptr_t virt_addr, size_t size, uint8_t flags);
    void free(void* ptr);
    bool handle_page_fault(uintptr_t fault_addr, size_t error_code);

   private:
    uintptr_t find_hole(size_t size, size_t alignment);
    uintptr_t find_hole(VmRegion* node, size_t size, size_t alignment);

    VmRegion* find_region_containing(uintptr_t addr);
    bool check_overlap(uintptr_t start, size_t size);

    void insert_region(uintptr_t start, size_t size, uint8_t flags, CacheType cache);
    void delete_node(VmRegion* z);

    void rotate_left(VmRegion* x);
    void rotate_right(VmRegion* x);
    void insert_fixup(VmRegion* z);
    void delete_fixup(VmRegion* x);

    void update_node_metadata(VmRegion* x);
    void update_path_to_root(VmRegion* x);

    VmRegion* predecessor(VmRegion* node);
    VmRegion* successor(VmRegion* node);

    void free_tree(VmRegion* node);

    Mutex mutex;
    PageMap* page_map;
    task::Process* process;
    VmRegion* root;
    VmRegion* cached_cursor;
    VmRegionAllocator metadata_allocator;
};
}  // namespace kernel::memory