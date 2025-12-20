#include "task/process.hpp"
#include "hal/smp_manager.hpp"
#include "libs/log.hpp"
#include "libs/spinlock.hpp"
#include "memory/memory.hpp"
#include "memory/pagemap.hpp"
#include "libs/math.hpp"
#include "boot/boot.h"
#include "memory/pcid_manager.hpp"

namespace kernel::task {
namespace {
size_t next_pid;
}  // namespace

Process* kernel_proc = nullptr;

Thread::Thread(Process* proc, void (*callback)(void*), void* args, void* ptr) {
    if (proc == nullptr) {
        PANIC("Task: Thread's parent process not present!");
    }

    this->priority = 0;
    this->owner    = proc;
    this->state    = Ready;

    this->last_run_timestamp   = 0;
    this->wait_start_timestamp = 0;

    proc->lock.lock();
    this->tid = proc->next_tid++;
    proc->lock.unlock();

    if (!ptr) {
        this->cpu = cpu::CpuCoreManager::get().get_current_core();
    } else {
        this->cpu = reinterpret_cast<cpu::PerCpuData*>(ptr);
    }

    this->arch_init(reinterpret_cast<uintptr_t>(callback), reinterpret_cast<uintptr_t>(args));
    this->owner->threads.push_back(this);
}

Process::Process(memory::PageMap* map) : map(map) {
    static bool kernel_proc_init = false;

    if (kernel_proc_init) {
        PANIC("Call Process::Process(memory::PageMap*) only once");
    }

    lock.lock();
    this->pid = next_pid++;
    lock.unlock();

    // Start at 4KB to leave page 0 unmapped
    size_t user_start = 0x1000;
    // End of Canonical Lower half
    size_t user_end = 0x00007FFFFFFFF000;

    this->user_vmm.init(user_start, user_end - user_start);

    // Array to store the PIC assigned to this process on each CPU.
    this->pcid_cache = new uint16_t[mp_request.response->cpu_count];

    for (uint16_t i = 0; i < mp_request.response->cpu_count; ++i) {
        this->pcid_cache[i] = 0;
    }

    kernel_proc_init = true;
}

Process::Process() {
    lock.lock();
    this->pid = next_pid++;
    lock.unlock();

    this->map = new memory::PageMap;
    memory::PageMap::create_new(this->map);

    // Array to store the PIC assigned to this process on each CPU.
    this->pcid_cache = new uint16_t[mp_request.response->cpu_count];

    for (uint16_t i = 0; i < mp_request.response->cpu_count; ++i) {
        this->pcid_cache[i] = static_cast<uint16_t>(-1);
    }
}

Process::~Process() {
    for (size_t i = 0; i < mp_request.response->cpu_count; ++i) {
        uint16_t id = this->pcid_cache[i];

        if (id > 0) {
            memory::PcidManager::get().free_pcid(id);
        }
    }
}

void Process::init() {
    kernel_proc = new Process(memory::PageMap::get_kernel_map());
}

void* Process::mmap(size_t count, memory::PageSize size, uint8_t flags) {
    using namespace memory;

    size_t align_bytes = 0;
    size_t step_bytes  = 0;

    switch (size) {
        case PageSize::Size4K:
            align_bytes = PAGE_SIZE_4K;
            step_bytes  = PAGE_SIZE_4K;
            break;
        case PageSize::Size2M:
            align_bytes = PAGE_SIZE_2M;
            step_bytes  = PAGE_SIZE_2M;
            break;
        case PageSize::Size1G:
            align_bytes = PAGE_SIZE_1G;
            step_bytes  = PAGE_SIZE_1G;
            break;
    }

    size_t total_bytes = count * step_bytes;

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
                uintptr_t addr = virt_addr + (j * step_bytes);
                this->map->unmap(addr, 0, true);
            }

            this->user_vmm.free_region(virt_addr, total_bytes);

            LOG_ERROR("VMM: failed to map page at 0x%lx (rolling back)", curr_virt);
            return nullptr;
        }

        curr_virt += step_bytes;
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