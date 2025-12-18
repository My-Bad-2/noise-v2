#include "task/process.hpp"
#include "hal/smp_manager.hpp"
#include "libs/log.hpp"
#include "libs/spinlock.hpp"
#include "memory/pagemap.hpp"
#include "boot/boot.h"
#include "memory/pcid_manager.hpp"

namespace kernel::task {
namespace {
static size_t next_pid;
}  // namespace

Process* kernel_proc = nullptr;

Thread::Thread(Process* proc, void (*callback)(void*), void* args, void* ptr) {
    if (proc == nullptr) {
        PANIC("Task: Thread's parent process not present!");
    }

    this->priority     = 0;
    this->owner        = proc;
    this->thread_state = Ready;

    proc->lock.lock();
    this->tid = proc->next_tid++;
    proc->lock.unlock();

    if (!ptr) {
        this->cpu = cpu::CpuCoreManager::get().get_current_core();
    } else {
        this->cpu = reinterpret_cast<cpu::PerCpuData*>(ptr);
    }

    arch_init(reinterpret_cast<uintptr_t>(callback), reinterpret_cast<uintptr_t>(args));
    this->owner->threads.push_back(this);
}

Process::Process(memory::PageMap* map) {
    static bool kernel_proc_init = false;

    if (kernel_proc_init) {
        PANIC("Call Process::Process(memory::PageMap*) only once");
    }

    lock.lock();
    this->pid = next_pid++;
    lock.unlock();

    this->map = map;

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
}  // namespace kernel::task