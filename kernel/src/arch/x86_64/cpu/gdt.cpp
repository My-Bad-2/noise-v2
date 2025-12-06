#include "cpu/gdt.hpp"
#include "cpu/cpu.hpp"
#include "libs/log.hpp"
#include <string.h>

extern "C" void load_gdt_and_flush(kernel::cpu::arch::GDTR* gdtr);

#define GDT_ACCESS_TSS 0x09  // 64-bit TSS (Available)

#define GDT_ACCESS_ACCESSED   (1 << 0)  // CPU Accessed Bit
#define GDT_ACCESS_READWRITE  (1 << 1)
#define GDT_ACCESS_CONFORMING (1 << 2)
#define GDT_ACCESS_EXECUTABLE (1 << 3)
#define GDT_ACCESS_SEGMENT    (1 << 4)
#define GDT_ACCESS_RING0      (0 << 5)
#define GDT_ACCESS_RING3      (3 << 5)
#define GDT_ACCESS_PRESENT    (1 << 7)

#define GDT_FLAG_LONG_MODE 0x20  // L bit
#define GDT_FLAG_PAGE_GRAN 0x80  // G bit

namespace kernel::cpu::arch {
void GDTManager::setup_gdt(CPUData* cpu) {
    // Lambda encodes a flat GDT entry; centralizing the bit layout here
    // keeps the rest of the code free from descriptor-level details.
    // NOLINTNEXTLINE
    auto encode_entry = [&](int idx, uint64_t base, uint64_t limit, uint8_t access, uint8_t flags) {
        cpu->gdt[idx].base_low    = (base & 0xFFFF);
        cpu->gdt[idx].base_middle = (base >> 16) & 0xFF;
        cpu->gdt[idx].base_high   = (base >> 24) & 0xFF;
        cpu->gdt[idx].limit_low   = (limit & 0xFFFF);
        cpu->gdt[idx].granularity = ((limit >> 16) & 0x0F) | (flags & 0xF0);
        cpu->gdt[idx].access      = access;
    };

    // Build a minimal flat GDT: null, kernel code/data, user code/data,
    // plus one TSS descriptor that points at the per-CPU TSS block.

    // 0: Null
    encode_entry(0, 0, 0, 0, 0);

    // 1: Kernel Code
    encode_entry(
        1, 0, 0,
        GDT_ACCESS_PRESENT | GDT_ACCESS_SEGMENT | GDT_ACCESS_READWRITE | GDT_ACCESS_EXECUTABLE,
        GDT_FLAG_LONG_MODE | GDT_FLAG_PAGE_GRAN);

    // 2: Kernel Data
    encode_entry(2, 0, 0, GDT_ACCESS_PRESENT | GDT_ACCESS_SEGMENT | GDT_ACCESS_READWRITE,
                 GDT_FLAG_PAGE_GRAN);

    // 3: User Code
    encode_entry(3, 0, 0,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_SEGMENT | GDT_ACCESS_READWRITE |
                     GDT_ACCESS_EXECUTABLE | GDT_ACCESS_RING3,
                 GDT_FLAG_LONG_MODE | GDT_FLAG_PAGE_GRAN);

    // 4: User Data
    encode_entry(4, 0, 0,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_SEGMENT | GDT_ACCESS_READWRITE | GDT_ACCESS_RING3,
                 GDT_FLAG_PAGE_GRAN);

    uint64_t tss_base  = reinterpret_cast<uint64_t>(&cpu->tss_block);
    uint64_t tss_limit = sizeof(TSSBlock) - 1;

    TSSDescriptor* tss_desc = reinterpret_cast<TSSDescriptor*>(&cpu->gdt[5]);
    tss_desc->limit_low     = (tss_limit & 0xFFFF);
    tss_desc->base_low      = (tss_base & 0xFFFF);
    tss_desc->base_middle   = (tss_base >> 16) & 0xFF;
    tss_desc->access        = GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_TSS;
    tss_desc->granularity   = (tss_limit >> 16) & 0x0F;
    tss_desc->base_high     = (tss_base >> 24) & 0xFF;
    tss_desc->base_upper    = (tss_base >> 32);
    tss_desc->reserved      = 0;
}

void GDTManager::setup_tss(CPUData* cpu, uint64_t stack_top) {
    TSSBlock& block = cpu->tss_block;

    // Point IOMAP base to the array immediately following the header
    block.header.iomap_base = sizeof(TSS64);

    // Set Ring 0 Stack (Used when interrupt moves Ring 3 -> Ring 0)
    block.header.rsp0 = stack_top;

    // Deny access to all ports by default; explicit calls to set_io_perm
    // grant access where needed.
    memset(block.iopb, 0xFF, IOPB_SIZE);
    block.terminator = 0xFF;
}

void GDTManager::load_tables(CPUData* cpu) {
    GDTR gdtr  = {};
    gdtr.base  = reinterpret_cast<uint64_t>(cpu->gdt);
    gdtr.limit = sizeof(cpu->gdt) - 1;

    // Install per-CPU GDT and reload segment registers in asm stub.
    load_gdt_and_flush(&gdtr);

    // Index 5 * 8 = 0x28
    asm volatile("mov $0x28, %%ax; ltr %%ax" ::: "ax");

    LOG_INFO("GDT: loaded per-CPU tables gdt=0x%lx tss=0x%lx",
             gdtr.base, reinterpret_cast<uint64_t>(&cpu->tss_block));
}

void GDTManager::set_io_perm(arch::CPUData* arch, uint16_t port, bool enable) {
    // Flip the corresponding bit in the per-CPU I/O bitmap. This lets
    // us selectively grant user-space access to legacy ports.
    if (enable) {
        arch->tss_block.iopb[port / 8] &= ~(1 << (port % 8));
    } else {
        arch->tss_block.iopb[port / 8] |= (1 << (port % 8));
    }

    LOG_DEBUG("GDT: I/O perm %s port=0x%x", enable ? "allow" : "deny", port);
}
}  // namespace kernel::cpu::arch