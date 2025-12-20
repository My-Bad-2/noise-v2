#include <atomic>
#include "arch.hpp"
#include "boot/boot.h"
#include "hal/smp_manager.hpp"
#include "libs/log.hpp"
#include "memory/pcid_manager.hpp"
#include "task/process.hpp"
#include "libs/math.hpp"
#include "task/scheduler.hpp"

namespace kernel::cpu {
namespace {
void idle_worker(void*) {
    kernel::arch::halt(true);
}

void worker(void* arg) {
    const char* name = static_cast<const char*>(arg);

    while (true) {
        LOG_DEBUG("Hello from Thread %s", name);

        task::Scheduler::get().sleep(20);
    }
}
}  // namespace

void PerCpuData::init(void* stack_top) {
    if (!stack_top) {
        std::byte* stack_base = new std::byte[KSTACK_SIZE];

        if (!stack_base) {
            PANIC("Cannot allocate stack for AP core idx %u", this->core_idx);
        }

        this->kstack_top = reinterpret_cast<uintptr_t>(stack_base) + KSTACK_SIZE;
    } else {
        this->kstack_top = reinterpret_cast<uintptr_t>(stack_top);
    }

    this->pcid_manager->init();
    this->sched.init(this->core_idx);

    this->idle_thread = this->curr_thread =
        new task::Thread(task::Process::kernel_proc, idle_worker, nullptr, this);

    this->reaper_thread = new task::Thread(
        task::Process::kernel_proc,
        [](void* arg) {
            task::Scheduler* sched = reinterpret_cast<task::Scheduler*>(arg);

            while (true) {
                sched->block();
                sched->reap_zombies();
            }
        },
        &this->sched, this);

    this->sched.add_thread(this->idle_thread);
    this->sched.add_thread(this->reaper_thread);

    this->arch_init();
}

void CpuCoreManager::init(void* bsp_stack_top) {
    if (!mp_request.response) {
        PANIC("Limine SMP Response is missing!");
    }

    size_t cpu_count = mp_request.response->cpu_count;
    this->cores.reserve(cpu_count);

    for (size_t i = 0; i < cpu_count; ++i) {
        limine_mp_info* info = mp_request.response->cpus[i];

        PerCpuData* data = new PerCpuData(static_cast<uint32_t>(i), info);
        this->cores.push_back(data);
    }

    for (size_t i = 0; i < this->cores.size(); ++i) {
        PerCpuData* core     = this->cores[i];
        limine_mp_info* info = mp_request.response->cpus[i];

        if (core->is_bsp) {
            core->init(bsp_stack_top);
        } else {
            core->init();
        }

        info->extra_argument = reinterpret_cast<uintptr_t>(core);

        if (core->is_bsp) {
            // Commit the CPU state immediately
            core->commit();
            this->init_syscalls();
        } else {
            // Launch the AP...
            info->goto_address = this->ap_entry_func;
        }

        while (!core->is_online.load(std::memory_order_acquire)) {
            kernel::arch::pause();
        }
    }

    // Uncomment these while testing any changes in scheduler
    // auto t1 = new task::Thread(task::Process::kernel_proc, worker, (void*)"A", cores[0]);
    // auto t2 = new task::Thread(task::Process::kernel_proc, worker, (void*)"B", cores[0]);

    // cores[0]->sched.add_thread(t1);
    // cores[0]->sched.add_thread(t2);
}

size_t CpuCoreManager::get_total_cores() const {
    return this->cores.size();
}

PerCpuData* CpuCoreManager::get_core_by_index(uint32_t idx) {
    return this->cores[idx];
}

CpuCoreManager& CpuCoreManager::get() {
    static CpuCoreManager manager;
    return manager;
}

extern "C" void check_reschedule() {
    PerCpuData* cpu = CpuCoreManager::get().get_current_core();

    // Check if a reschedule is needed
    if (cpu->reschedule_needed) {
        cpu->reschedule_needed = false;

        // This will save the current context and jump to the next thread.
        cpu->sched.schedule();
    }
}
}  // namespace kernel::cpu