#include "task/process.hpp"
#include "hal/cpu.hpp"
#include "libs/spinlock.hpp"
#include "memory/pagemap.hpp"

namespace kernel::task {
namespace {
static size_t next_tid;
static size_t next_pid;
SpinLock lock;
}  // namespace

Thread::Thread(Process* proc, void (*callback)(void*), void* args) {
    this->parent       = proc;
    this->thread_state = Ready;

    lock.lock();
    this->tid = next_tid++;
    lock.unlock();

    this->cpu = cpu::CPUCoreManager::get_curr_cpu();

    arch_init(reinterpret_cast<uintptr_t>(callback), reinterpret_cast<uintptr_t>(args));
}

Process::Process() {
    lock.lock();
    this->pid = next_pid++;
    lock.unlock();

    memory::PageMap::create_new(&this->map);
}

Process::~Process() {
    this->threads.clear();
    this->map.~PageMap();

    // Unsafe af
    delete this;
}
}  // namespace kernel::task