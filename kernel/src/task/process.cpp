#include "task/process.hpp"
#include "boot/boot.h"
#include "memory/memory.hpp"
#include "memory/pagemap.hpp"
#include "memory/vma.hpp"
#include "libs/math.hpp"
#include "libs/log.hpp"
#include "memory/pcid_manager.hpp"
#include <string.h>

namespace kernel::task {
Process* Process::kernel_proc         = nullptr;
std::atomic<size_t> Process::next_pid = 0;

Thread::Thread(Process* proc, void (*callback)(void*), void* args) {
    if (proc == nullptr) {
        PANIC("Task: Thread's parent process not present!");
    }

    this->owner    = proc;
    this->priority = 0;
    this->state    = Ready;

    // Only Kernel Process (PID 0) is permitted to create kernel threads
    this->is_user_thread = (proc->pid != 0);

    this->last_run_timestamp   = 0;
    this->wait_start_timestamp = 0;

    {
        LockGuard guard(proc->lock);
        this->tid = proc->next_tid++;
        proc->threads.push_back(*this);
    }

    this->arch_init(reinterpret_cast<uintptr_t>(callback), reinterpret_cast<uintptr_t>(args));
}

Thread::~Thread() {
    if (this->is_user_thread) {
        this->owner->munmap(this->kernel_stack, USTACK_SIZE);
    } else {
        delete this->kernel_stack;
    }

    delete this->fpu_storage;
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
    uint8_t flag = memory::Lazy;

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

    if (flags & MAP_POPULATE) {
        flags &= ~memory::Lazy;
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

int Process::mprotect(void* addr, size_t len, int prot) {
    uintptr_t virt_start = reinterpret_cast<uintptr_t>(addr);

    if (virt_start & (memory::PAGE_SIZE_4K - 1)) {
        return -1;
    }

    uint8_t flags           = memory::Read | memory::User;
    memory::CacheType cache = memory::CacheType::WriteBack;

    // Align length to 4KiB
    size_t aligned_len = align_up(len, memory::PAGE_SIZE_4K);

    if (prot & PROT_WRITE) {
        flags |= memory::Write;
    }

    if (prot & PROT_EXEC) {
        flags |= memory::Execute;
    }

    uintptr_t curr_virt  = virt_start;
    uintptr_t virt_end   = virt_start + aligned_len;
    uint16_t active_pcid = memory::PcidManager::get().get_pcid(this);

    // TODO: Implement shatter page in PageMap
    while (curr_virt < virt_end) {
        size_t chunk_size = this->map->set_page_flags(curr_virt, flags, cache, active_pcid);

        if (chunk_size == 0) {
            return -1;
        }

        curr_virt += chunk_size;
    }

    return 0;
}
}  // namespace kernel::task