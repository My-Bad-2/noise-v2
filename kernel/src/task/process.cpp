#include "task/process.hpp"
#include "hal/cpu.hpp"
#include "libs/log.hpp"
#include "libs/spinlock.hpp"
#include "memory/pagemap.hpp"

namespace kernel::task {
namespace {
static size_t next_pid;
}  // namespace

Process* kernel_proc = nullptr;

Thread::Thread(Process* proc, void (*callback)(void*), void* args) {
    if (proc == nullptr) {
        PANIC("Task: Thread's parent process not present!");
    }

    this->priority     = 0;
    this->owner        = proc;
    this->thread_state = Ready;

    proc->lock.lock();
    this->tid = proc->next_tid++;
    proc->lock.unlock();

    this->cpu = cpu::CPUCoreManager::get_curr_cpu();

    arch_init(reinterpret_cast<uintptr_t>(callback), reinterpret_cast<uintptr_t>(args));
    this->owner->threads.push_back(this);
}

Process::Process(memory::PageMap* map) {
    lock.lock();
    this->pid = next_pid++;
    lock.unlock();

    this->map = map;
}

Process::Process() {
    lock.lock();
    this->pid = next_pid++;
    lock.unlock();

    this->map = new memory::PageMap;
    memory::PageMap::create_new(this->map);
}

void Process::init() {
    kernel_proc = new Process(memory::PageMap::get_kernel_map());
}
}  // namespace kernel::task