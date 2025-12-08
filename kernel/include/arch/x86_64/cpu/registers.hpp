/**
 * @file registers.hpp
 * @brief Strongly-typed wrappers around x86_64 control registers and MSRs.
 *
 * Instead of manually shuffling raw 64-bit values in inline assembly,
 * these structures provide:
 *  - Bitfield views for commonly used control bits.
 *  - `read()`/`write()` helpers that encapsulate the asm instructions.
 *
 * Why:
 *  - Reduces the risk of subtle mistakes when toggling paging, NX, PCID,
 *    SMEP/SMAP, etc.
 *  - Makes call sites self-documenting (e.g. `cr4.smep = true`).
 */

#pragma once

#include <cstdint>

namespace kernel::arch {

// CR0: basic CPU control (paging, write-protection, etc.).
struct Cr0 {
    union {
        uint64_t raw;
        struct {
            // The bit layout matches Intel/AMD manuals; only select fields
            // are named to avoid overconstraining future use.
            uint64_t protected_mode      : 1;
            uint64_t monitor_coprocessor : 1;
            uint64_t emulation           : 1;
            uint64_t task_switched       : 1;
            uint64_t extension_type      : 1;
            uint64_t numeric_error       : 1;
            uint64_t rsvd1               : 10;
            uint64_t write_protected     : 1;
            uint64_t rsvd2               : 1;
            uint64_t alignment_mask      : 1;
            uint64_t rsvd3               : 10;
            uint64_t not_write_through   : 1;
            uint64_t cache_disable       : 1;
            uint64_t paging              : 1;
            uint64_t rsvd4               : 32;
        } __attribute__((packed));
    };

    static Cr0 read();
    void write();
};

// CR2: holds the faulting linear address on page faults.
struct Cr2 {
    uint64_t linear_address;

    static Cr2 read();
    void write();
};

// CR3: holds the root paging-structure physical address, optionally PCID.
struct Cr3 {
    union {
        uint64_t raw;

        struct {
            uint64_t ignored_1 : 3;
            uint64_t pwt       : 1;
            uint64_t pcd       : 1;
            uint64_t ignored_2 : 7;
            uint64_t base_addr : 52;
        } __attribute__((packed)) standard;

        struct {
            // When PCID is enabled, CR3 becomes a (PCID, base, no-flush)
            // triple that controls both the active address space and
            // how aggressively TLB entries are retained across switches.
            uint64_t pcid      : 12;
            uint64_t base_addr : 51;
            uint64_t no_flush  : 1;
        } __attribute__((packed)) pcid_enabled;
    };

    static Cr3 read();
    void write();
};

// CR4: extended CPU control flags (PGE, SMEP, PCID, etc.).
struct Cr4 {
    union {
        uint64_t raw;
        struct {
            uint64_t vme        : 1;
            uint64_t pvi        : 1;
            uint64_t tsd        : 1;
            uint64_t de         : 1;
            uint64_t pse        : 1;
            uint64_t pae        : 1;
            uint64_t mce        : 1;
            uint64_t pge        : 1;
            uint64_t pce        : 1;
            uint64_t osfxsr     : 1;
            uint64_t osxmmexcpt : 1;
            uint64_t umip       : 1;
            uint64_t la57       : 1;
            uint64_t vmx_enable : 1;  // Standard name: VMXE
            uint64_t smx_enable : 1;  // Standard name: SMXE
            uint64_t rsvd1      : 1;
            uint64_t fs_gs_base : 1;
            uint64_t pcide      : 1;
            uint64_t osxsave    : 1;
            uint64_t key_locker : 1;  // Key Locker
            uint64_t smep       : 1;
            uint64_t smap       : 1;
            uint64_t pke        : 1;  // Protection Key Enable (User)
            uint64_t cet        : 1;  // Control-flow Enforcement Technology
            uint64_t pks        : 1;  // Protection Keys for Supervisor
            uint64_t uintr      : 1;  // User Interrupts
            uint64_t rsvd2      : 38;
        } __attribute__((packed));
    };

    static Cr4 read();
    void write();
};

// INVPCID descriptor and type: model the hardware format directly.
enum class InvpcidType : uint8_t {
    IndivdualAddress         = 0,
    SingleContext            = 1,
    AllContexts              = 2,
    AllContextsRetainGlobals = 3,
};

struct InvpcidDesc {
    uint64_t pcid : 12;
    uint64_t rsvd : 52;
    uint64_t addr;

    /**
     * @brief Issue INVPCID for this descriptor and @p type.
     *
     * Why:
     *  - Allows precise TLB invalidation (by address, by PCID, or all)
     *    without the heavy-handed cost of reloading CR3 everywhere.
     */
    void flush(InvpcidType type);
};

// MSR wrapper: encapsulates rdmsr/wrmsr usage for a single index.
struct Msr {
    uint32_t index;
    uint64_t value;

    /**
     * @brief Read an MSR into a typed wrapper.
     *
     * Commonly used for EFER, PAT, APIC base, etc., so callers can
     * manipulate fields in `value` and write them back.
     */
    static Msr read(uint32_t index);
    /// Write the stored 64-bit value back to `index`.
    void write();
};

// MXCSR: Controls SIMD floating-point exceptions, rounding modes, and flag status.
struct Mxcsr {
    union {
        uint32_t raw;
        struct {
            // Sticky Exception Flags (Bits 0-5)
            // Set by CPU when exception occurs. Must be cleared manually by the kernel.
            uint32_t invalid_operation_flag : 1;  // IE
            uint32_t denormal_flag          : 1;  // DE
            uint32_t divide_by_zero_flag    : 1;  // ZE
            uint32_t overflow_flag          : 1;  // OE
            uint32_t underflow_flag         : 1;  // UE
            uint32_t precision_flag         : 1;  // PE

            // Denormals Are Zeros (Bit 6)
            // If 1, denormal inputs are treated as 0.0 (performance optimization).
            uint32_t daz : 1;

            // Exception Masks (Bits 7-12)
            // If 1, the exception is masked (suppressed/handled by hardware default).
            // If 0, the exception causes a hardware trap (#XM).
            uint32_t invalid_operation_mask : 1;  // IM
            uint32_t denormal_mask          : 1;  // DM
            uint32_t divide_by_zero_mask    : 1;  // ZM
            uint32_t overflow_mask          : 1;  // OM
            uint32_t underflow_mask         : 1;  // UM
            uint32_t precision_mask         : 1;  // PM

            // Rounding Control (Bits 13-14)
            // 00: Nearest, 01: Down (-inf), 10: Up (+inf), 11: Truncate (toward 0)
            uint32_t rounding_control : 2;  // RC

            // Flush To Zero (Bit 15)
            // If 1, denormal results are set to 0.0 (performance optimization).
            uint32_t ftz : 1;

            uint32_t rsvd : 16;
        } __attribute__((packed));
    };

    enum RoundingMode : uint8_t {
        ROUND_NEAREST = 0b00,
        ROUND_DOWN    = 0b01,
        ROUND_UP      = 0b10,
        ROUND_TRUNC   = 0b11
    };

    static Mxcsr read();
    void write();
};

#include <stdint.h>

// XCR0: Configures the user-state components that the processor is allowed to
// manage via XSAVE/XRSTOR instructions.
struct Xcr0 {
    union {
        uint64_t raw;
        struct {
            // Legacy x87 Floating Point (Must be 1)
            uint64_t x87 : 1;  // Bit 0: FCW/FSW/FTW/FOP/FIP/FDP/MMX

            // SSE State (Must be 1 for AVX)
            uint64_t sse : 1;  // Bit 1: XMM0-XMM15 + MXCSR

            // AVX State (YMM)
            uint64_t avx : 1;  // Bit 2: Upper halves of YMM0-YMM15

            // MPX (Memory Protection Extensions) - Deprecated on some modern CPUs
            uint64_t bndreg : 1;  // Bit 3: BND0-BND3
            uint64_t bndcsr : 1;  // Bit 4: BNDCFGU/BNDSTATUS

            // AVX-512 State
            uint64_t opmask    : 1;  // Bit 5: k0-k7 (OpMask)
            uint64_t zmm_hi256 : 1;  // Bit 6: Upper halves of ZMM0-ZMM15
            uint64_t hi16_zmm  : 1;  // Bit 7: ZMM16-ZMM31 (full registers)

            uint64_t rsvd1 : 1;  // Bit 8: Reserved

            // Protection Keys (PKRU)
            uint64_t pkru : 1;  // Bit 9: User-mode protection keys

            uint64_t rsvd2 : 1;  // Bit 10: Reserved

            // Control-flow Enforcement Technology (CET)
            uint64_t cet_u : 1;  // Bit 11: User CET state (U_CET/SSP)
            uint64_t cet_s : 1;  // Bit 12: Supervisor CET state (PL0_SSP/etc)

            uint64_t rsvd3 : 4;  // Bits 13-16: Reserved

            // Advanced Matrix Extensions (AMX)
            uint64_t tilecfg  : 1;  // Bit 17: TILECFG
            uint64_t tiledata : 1;  // Bit 18: TILEDATA (Palette 0)

            uint64_t rsvd4 : 45;  // Bits 19-63: Reserved
        } __attribute__((packed));
    };

    static Xcr0 read();

    /**
     * @brief Program XCR0 via `xsetbv`.
     *
     * Why:
     *  - Controls which extended state components XSAVE/XRSTOR manage,
     *    and thus how large thread save-areas must be.
     *
     * Warning:
     *  - Requires CR4.OSXSAVE=1 and feature bits to be present, otherwise
     *    `xsetbv` will fault. Callers must perform CPUID checks first.
     */
    void write();
};
}  // namespace kernel::arch