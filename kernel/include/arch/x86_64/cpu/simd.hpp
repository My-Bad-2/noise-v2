#pragma once

#include <stdint.h>

namespace kernel::cpu::arch {
/**
 * @brief Enumerates the SIMD/FPU state model in use.
 *
 * The mode determines:
 *  - Which instructions are safe/required to touch (x87, SSE, AVX…).
 *  - How large a save area is needed for context switching.
 *
 * Why:
 *  - Some CPUs or configs may only support x87/SSE, while others expose
 *    AVX/AVX-512 and extended XSAVE features. The kernel needs a single
 *    knob that records "how much state must be preserved per thread".
 */
enum FpuMode : uint8_t {
    None,       ///< FPU/SIMD not configured (early boot / fallback).
    LegacyX87,  ///< Only x87 state is used/saved.
    SSE,        ///< x87 + SSE (FXSAVE/FXRSTOR).
    AVX,        ///< AVX enabled (XSAVE/XRSTOR with AVX bits).
    AVXOpt,     ///< AVX + optional extensions (e.g. AVX2/AVX-512) via XCR0.
};

/**
 * @brief SIMD/FPU state management helper.
 *
 * Provides a small facade for:
 *  - Detecting available SIMD features and configuring XCR0/MXCSR.
 *  - Reporting how big a save area is required (`get_save_size`).
 *  - Saving/restoring per-thread SIMD state for context switches.
 *
 * Why:
 *  - Keeps low-level XSAVE/FXSAVE usage in one place so the scheduler
 *    and thread code don't need to know which exact instruction set
 *    is active on a given machine.
 */
class SIMD {
   public:
    /**
     * @brief Initialize SIMD/FPU support on the current CPU.
     *
     * Typically called during per-CPU bring-up. It probes CPUID/XCR0,
     * chooses an `FpuMode`, programs CR4.OSXSAVE/XCR0 where appropriate,
     * and computes `save_size`.
     */
    static void init();

    /**
     * @brief Save the current CPU's SIMD/FPU state into @p buffer.
     *
     * The caller must provide a buffer of at least `get_save_size()`
     * bytes, correctly aligned for the selected mode. This is intended
     * for use by the scheduler on context switch.
     */
    static void save(void* buffer);

    /**
     * @brief Restore SIMD/FPU state from @p buffer into the current CPU.
     *
     * Counterpart to `save()`. The buffer must have been populated by a
     * previous call to `save()` using the same mode and CPU.
     */
    static void restore(void* buffer);

    /// Return the number of bytes required to save the current SIMD state.
    static uint32_t get_save_size() {
        return save_size;
    }

   private:
    /// Size in bytes of the current save area (FXSAVE vs XSAVE, etc.).
    static uint32_t save_size;
    /// Active FPU/SIMD mode derived from CPUID/XCR0.
    static FpuMode mode;
};
}  // namespace kernel::cpu::arch