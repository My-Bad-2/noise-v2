#pragma once

#include <cstdint>
#include "cpu/cpu.hpp"

namespace kernel::task {
struct Thread;
}

namespace kernel::cpu {
extern bool initialized;
/**
 * @brief Generic per‑CPU data visible to higher layers.
 *
 * Layout:
 *  - `self`     points to this structure (useful when loaded in a TLS
 *               or segment register).
 *  - `cpu_id`   logical CPU identifier.
 *  - `status_flag` simple status word (booted/online flags etc.).
 *  - `arch`     architecture‑specific state (`CPUData` with GDT/TSS).
 *
 * Why:
 *  - Provides a single anchor for all per‑CPU state, allowing quick
 *    lookup (e.g. via `GS.base`) and straightforward extension.
 */
struct alignas(CACHE_LINE_SIZE) PerCPUData {
    PerCPUData* self;
    uint32_t cpu_id;
    uint32_t status_flag;
    task::Thread* curr_thread;
    task::Thread* idle_thread;
    arch::CPUData arch;
};

/**
 * @brief High‑level CPU/core management helpers.
 *
 * Today this is a thin wrapper around per‑CPU init and I/O permission
 * control; in an SMP system it becomes the place to bring up and manage
 * multiple cores.
 */
class CPUCoreManager {
   public:
    /**
     * @brief Allocate and initialize per‑CPU state for a core.
     *
     * Sets up `PerCPUData`, programs GDT/TSS via `arch::CPUData`, and
     * commits the new tables to hardware.
     */
    static PerCPUData* init_core(uint32_t cpu_id, uintptr_t stack_top);

    /**
     * @brief Enable or disable access to an I/O port for a CPU.
     *
     * This edits the TSS I/O bitmap, gating which legacy ports code
     * running on this CPU is allowed to touch in ring3.
     */
    static void allow_io_port(PerCPUData* cpu, uint16_t port, bool enable);

    /**
     * @brief Get the current CPU's logical ID using the GS‑based TLS.
     *
     * Relies on `GS.base` pointing at the active `PerCPUData`.
     */
    static uint32_t get_curr_cpu_id();

    /**
     * @brief Get a pointer to the current CPU's `PerCPUData`.
     *
     * This is a convenience wrapper that uses `GS.base` to locate the
     * active per‑CPU structure and is typically used by low-level code
     * that needs CPU-local state (scheduler, interrupt handlers).
     */
    static PerCPUData* get_curr_cpu();
};
}  // namespace kernel::cpu