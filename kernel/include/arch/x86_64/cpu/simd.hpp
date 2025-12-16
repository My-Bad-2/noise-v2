#pragma once

#include <stdint.h>

namespace kernel::cpu::arch {
enum FpuMode : uint8_t {
    None,       ///< FPU/SIMD not configured (early boot / fallback).
    LegacyX87,  ///< Only x87 state is used/saved.
    SSE,        ///< x87 + SSE (FXSAVE/FXRSTOR).
    AVX,        ///< AVX enabled (XSAVE/XRSTOR with AVX bits).
    AVXOpt,     ///< AVX + optional extensions (e.g. AVX2/AVX-512) via XCR0.
};

class SIMD {
   public:
    static void init();

    static void save(void* buffer);
    static void restore(void* buffer);

    static uint32_t get_save_size() {
        return save_size;
    }

   private:
    // Size in bytes of the current save area (FXSAVE vs XSAVE, etc.).
    static uint32_t save_size;
    static FpuMode mode;
};
}  // namespace kernel::cpu::arch