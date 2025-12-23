#pragma once

#include <atomic>
#include "memory/pagemap.hpp"
#include "libs/spinlock.hpp"
#include "libs/intrusive_list.hpp"
#include "memory/user_address_space.hpp"

#define PROT_READ  0x01
#define PROT_WRITE 0x02
#define PROT_EXEC  0x04
#define PROT_NONE  0x08

#define MAP_HUGE_2MB 0x01
#define MAP_HUGE_1GB 0x02

namespace kernel::cpu {
struct PerCpuData;
}

namespace kernel::task {
struct Process;

enum ThreadState : uint32_t {
    Ready    = (1 << 0),
    Running  = (1 << 1),
    Blocked  = (1 << 2),
    Sleeping = (1 << 3),
    Zombie   = (1 << 4),
};

struct SchedulerTag {};
struct ProcessTag {};
struct WaitTag {};

struct Thread : public IntrusiveListNode<SchedulerTag>,
                public IntrusiveListNode<ProcessTag>,
                public IntrusiveListNode<WaitTag> {
    size_t tid;
    uintptr_t kernel_stack_ptr;

    cpu::PerCpuData* cpu;
    ThreadState state;
    uint16_t priority;
    uint16_t quantum;

    std::byte* fpu_storage;
    size_t wake_time_ticks;
    size_t wait_start_timestamp;
    size_t last_run_timestamp;

    Process* owner;
    std::byte* kernel_stack;
    bool is_user_thread;

    Thread() = default;
    Thread(Process* parent, void (*callback)(void*), void* args, void* curr_cpu = nullptr,
           bool is_user = false);
    ~Thread() {}

   private:
    void arch_init(uintptr_t entry, uintptr_t arg);

    static std::byte* clean_fpu_state;
    static size_t fpu_state_size;
    static std::align_val_t fpu_alignment;
};

struct Process : public IntrusiveListNode<ProcessTag> {
    size_t pid;
    memory::PageMap* map;
    SpinLock lock;

    uint16_t* pcid_cache;
    std::atomic<size_t> next_tid;

    IntrusiveList<Process, ProcessTag> children;
    IntrusiveList<Thread, ProcessTag> threads;
    memory::UserAddressSpace vma;

    int exit_code;

    static Process* kernel_proc;

    Process(memory::PageMap* map);  // Kernel
    Process();                      // User
    ~Process();

    void* mmap(void* addr, size_t len, int prot, int flags);
    void munmap(void* ptr, size_t len);

    static void init();

   private:
    static std::atomic<size_t> next_pid;
};
}  // namespace kernel::task