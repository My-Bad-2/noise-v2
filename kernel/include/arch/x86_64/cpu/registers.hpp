/**
 * @file registers.hpp
 * @brief Strongly-typed wrappers around x86_64 control registers and MSRs.
 *
 * Instead of manually shuffling raw 64-bit values in inline assembly,
 * these structures provide:
 *  - Bitfield views for commonly used control bits.
 *  - `read()`/`write()` helpers that encapsulate the asm instructions.
 *
 * This reduces the risk of mistakes when toggling paging, NX, PCID, etc.
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
    IndivdualAddress       = 0,
    SingleContext          = 1,
    AllContexts            = 2,
    AllContextsRetainGlobals = 3,
};

struct InvpcidDesc {
    uint64_t pcid : 12;
    uint64_t rsvd : 52;
    uint64_t addr;

    void flush(InvpcidType type);
};

// MSR wrapper: encapsulates rdmsr/wrmsr usage for a single index.
struct Msr {
    uint32_t index;
    uint64_t value;

    static Msr read(uint32_t index);
    void write();
};
}  // namespace kernel::arch