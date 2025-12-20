#include "task/process.hpp"
#include <atomic>
#include "hal/smp_manager.hpp"
#include "libs/log.hpp"
#include "libs/spinlock.hpp"
#include "memory/memory.hpp"
#include "memory/pagemap.hpp"
#include "libs/math.hpp"
#include "boot/boot.h"
#include "memory/pcid_manager.hpp"

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

    size_t user_start = 0x1000;
    size_t user_end   = 0x00007FFFFFFFF000;
    this->user_vmm.init(user_start, user_end - user_start);

    // Array to store the PICD assigned to this process on each CPU.
    size_t cpu_count = mp_request.response->cpu_count;
    this->pcid_cache = new uint16_t[cpu_count];

    // Since this is a kernel process, PCID is 0
    memset(this->pcid_cache, 0, cpu_count * sizeof(uint16_t));
}

Process::Process() {
    this->pid = next_pid.fetch_add(1, std::memory_order_relaxed);
    this->next_tid.store(1, std::memory_order_relaxed);

    this->map = new memory::PageMap;
    memory::PageMap::create_new(this->map);

    size_t user_start = 0x1000;
    size_t user_end   = 0x00007FFFFFFFF000;
    this->user_vmm.init(user_start, user_end - user_start);

    // Array to store the PICD assigned to this process on each CPU.
    size_t cpu_count = mp_request.response->cpu_count;
    this->pcid_cache = new uint16_t[cpu_count];

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

void Process::init() {
    kernel_proc = new Process(memory::PageMap::get_kernel_map());
}

void* Process::mmap(size_t count, memory::PageSize size, uint8_t flags) {
    using namespace memory;

    size_t align_bytes = 0;

    switch (size) {
        case PageSize::Size4K:
            align_bytes = PAGE_SIZE_4K;
            break;
        case PageSize::Size2M:
            align_bytes = PAGE_SIZE_2M;
            break;
        case PageSize::Size1G:
            align_bytes = PAGE_SIZE_1G;
            break;
    }

    size_t total_bytes = count * align_bytes;

    // First, reserve a virtual range from the allocator.
    uintptr_t virt_addr = this->user_vmm.alloc_region(total_bytes, align_bytes);

    if (virt_addr == 0) {
        return nullptr;
    }

    // Force the page to be readable and user-accessible
    flags |= memory::Read | memory::User;

    uintptr_t curr_virt = virt_addr;

    // Then back each page with physical memory via the process page map.
    for (size_t i = 0; i < count; ++i) {
        if (!this->map->map(curr_virt, flags, CacheType::WriteBack, size)) {
            // Roll back any mappings we already created.
            for (size_t j = 0; j < i; ++j) {
                uintptr_t addr = virt_addr + (j * align_bytes);
                this->map->unmap(addr, 0, true);
            }

            this->user_vmm.free_region(virt_addr, total_bytes);
            return nullptr;
        }

        curr_virt += align_bytes;
    }

    return reinterpret_cast<void*>(virt_addr);
}

void Process::munmap(void* ptr, size_t count, memory::PageSize size) {
    using namespace memory;

    uintptr_t virt_addr = reinterpret_cast<uintptr_t>(ptr);
    size_t step_bytes   = 0;

    switch (size) {
        case PageSize::Size4K:
            step_bytes = PAGE_SIZE_4K;
            break;
        case PageSize::Size2M:
            step_bytes = PAGE_SIZE_2M;
            break;
        case PageSize::Size1G:
            step_bytes = PAGE_SIZE_1G;
            break;
    }

    size_t total_bytes = count * step_bytes;

    // Tear down mappings and free backing physical pages.
    for (size_t i = 0; i < count; ++i) {
        uintptr_t addr = virt_addr + (i * step_bytes);
        this->map->unmap(addr, 0, true);
    }

    // Return the virtual range to the allocator for reuse.
    this->user_vmm.free_region(virt_addr, total_bytes);
}
}  // namespace kernel::task