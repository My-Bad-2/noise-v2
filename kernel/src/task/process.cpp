#include "hal/smp_manager.hpp"
#include "boot/boot.h"
#include "memory/vma.hpp"

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

void Process::init() {
    kernel_proc = new Process(memory::PageMap::get_kernel_map());
}
}  // namespace kernel::task