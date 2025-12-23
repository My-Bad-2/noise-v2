#include "task/process.hpp"
#include "hal/smp_manager.hpp"
#include "boot/boot.h"
#include "memory/memory.hpp"
#include "memory/vma.hpp"
#include "libs/math.hpp"

namespace kernel::task {
Process* Process::kernel_proc         = nullptr;
std::atomic<size_t> Process::next_pid = 0;

Thread::Thread(Process* proc, void (*callback)(void*), void* args, void* ptr, bool is_user) {
    if (proc == nullptr) {
        PANIC("Task: Thread's parent process not present!");
    }

    this->owner          = proc;
    this->priority       = 0;
    this->state          = Ready;
    this->is_user_thread = is_user;

    this->last_run_timestamp   = 0;
    this->wait_start_timestamp = 0;

    if (!ptr) {
        this->cpu = cpu::CpuCoreManager::get().get_current_core();
    } else {
        this->cpu = reinterpret_cast<cpu::PerCpuData*>(ptr);
    }

    {
        LockGuard guard(proc->lock);
        this->tid = proc->next_tid++;
        proc->threads.push_back(*this);
    }

    this->arch_init(reinterpret_cast<uintptr_t>(callback), reinterpret_cast<uintptr_t>(args));
}

Process::Process(memory::PageMap* map) : map(map) {
    this->pid = next_pid.fetch_add(1, std::memory_order_relaxed);
    this->next_tid.store(1, std::memory_order_relaxed);

    // Array to store the PICD assigned to this process on each CPU.
    size_t cpu_count = mp_request.response->cpu_count;
    this->pcid_cache = new uint16_t[cpu_count];

    this->vma.init(this);
    memory::UserAddressSpace::arch_init();

    // Since this is a kernel process, PCID is 0
    memset(this->pcid_cache, 0, cpu_count * sizeof(uint16_t));
}

Process::Process() {
    this->pid = next_pid.fetch_add(1, std::memory_order_relaxed);
    this->next_tid.store(1, std::memory_order_relaxed);

    this->map = new memory::PageMap;
    memory::PageMap::create_new(this->map);

    // Array to store the PICD assigned to this process on each CPU.
    size_t cpu_count = mp_request.response->cpu_count;
    this->pcid_cache = new uint16_t[cpu_count];

    this->vma.init(this);

    memset(this->pcid_cache, 0xff, cpu_count * sizeof(uint16_t));
}

Process::~Process() {
    if (this->pcid_cache) {
        for (size_t i = 0; i < mp_request.response->cpu_count; ++i) {
            uint16_t id = this->pcid_cache[i];

            if ((id > 0) && id != static_cast<uint16_t>(-1)) {
                memory::PcidManager::get().free_pcid(id);
            }

            delete[] this->pcid_cache;
        }
    }

    while (!this->children.empty()) {
        // Manually delete threads and children until a separate reaper
        // mechanism is implemented.
        delete &children.front();
    }
}

void* Process::mmap(void* addr, size_t len, int prot, int flags) {
    uint8_t flag = 0;

    if (prot & PROT_READ) {
        flag |= memory::Read;
    }

    if (prot & PROT_WRITE) {
        flag |= memory::Write;
    }

    if (prot & PROT_EXEC) {
        flag |= memory::Execute;
    }

    if (prot & PROT_NONE) {
        // Do nothing
    }

    memory::PageSize type = memory::PageSize::Size4K;
    uintptr_t page_size   = memory::PAGE_SIZE_4K;

    if (flags & MAP_HUGE_2MB) {
        type      = memory::PageSize::Size2M;
        page_size = memory::PAGE_SIZE_2M;
    }

    if (flags & MAP_HUGE_1GB) {
        type      = memory::PageSize::Size1G;
        page_size = memory::PAGE_SIZE_1G;
    }

    size_t aligned_size = align_up(len, page_size);
    void* ret           = nullptr;

    if (addr) {
        if (this->vma.allocate_specific(reinterpret_cast<uintptr_t>(addr), aligned_size, flag,
                                        type)) {
            ret = addr;
        }
    } else {
        ret = this->vma.allocate(aligned_size, flag, type);
    }

    return ret;
}

void Process::munmap(void* ptr, size_t) {
    return this->vma.free(ptr);
}

void Process::init() {
    kernel_proc = new Process(memory::PageMap::get_kernel_map());
}
}  // namespace kernel::task