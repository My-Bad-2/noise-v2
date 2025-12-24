#pragma once

#include "memory/pagemap.hpp"
#include "libs/mutex.hpp"
#include "libs/spinlock.hpp"
#include "memory/pagemap.hpp"

namespace kernel::task {
struct Process;
}

namespace kernel::memory {
struct UserVmRegion {
    uintptr_t start;
    size_t size;

    size_t gap;
    size_t subtree_max_gap;

    uint8_t flags;
    PageSize page_size;
    CacheType cache;

    bool is_red          = true;
    UserVmRegion* parent = nullptr;
    UserVmRegion* left   = nullptr;
    UserVmRegion* right  = nullptr;

    uintptr_t end() const {
        return this->start + this->size;
    }
};

class UserVmRegionAllocator {
   public:
    UserVmRegion* allocate();
    void deallocate(UserVmRegion* node);

   private:
    void refill();

    struct FreeNode {
        FreeNode* next;
    };

    FreeNode* free_head = nullptr;
    SpinLock lock;
};

class UserAddressSpace {
   public:
    static constexpr uintptr_t USER_START = 0x1000;
    static constexpr uintptr_t USER_END   = 0x00007FFFFFFFFFFF;

    static void arch_init();

    void init(task::Process* proc);
    ~UserAddressSpace();

    void* allocate(size_t size, uint8_t flags = Read | Write, PageSize type = PageSize::Size4K);
    bool allocate_specific(uintptr_t virt_addr, size_t size, uint8_t flags,
                           PageSize type = PageSize::Size4K);
    void free(void* ptr);
    bool handle_page_fault(uintptr_t fault_addr, size_t error_code);

   private:
    uintptr_t find_hole(size_t size, size_t alignment);
    uintptr_t find_hole(UserVmRegion* node, size_t size, size_t alignment);

    bool populate(uintptr_t start, size_t size, uint8_t flags, CacheType cache);

    UserVmRegion* find_region_containing(uintptr_t addr);
    bool check_overlap(uintptr_t start, size_t size);

    void insert_region(uintptr_t start, size_t size, uint8_t flags, CacheType cache, PageSize type);
    void delete_node(UserVmRegion* z);

    void rotate_left(UserVmRegion* x);
    void rotate_right(UserVmRegion* x);
    void insert_fixup(UserVmRegion* z);
    void delete_fixup(UserVmRegion* x);

    void update_node_metadata(UserVmRegion* x);
    void update_path_to_root(UserVmRegion* x);

    UserVmRegion* predecessor(UserVmRegion* node);
    UserVmRegion* successor(UserVmRegion* node);

    void free_tree(UserVmRegion* node);

    Mutex mutex;
    PageMap* page_map;
    task::Process* process;
    UserVmRegion* root;
    UserVmRegion* cached_cursor;
    UserVmRegionAllocator metadata_allocator;
};
}  // namespace kernel::memory