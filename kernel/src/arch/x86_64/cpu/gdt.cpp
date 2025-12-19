#include "cpu/gdt.hpp"
#include "libs/log.hpp"
#include <string.h>

extern "C" void load_gdt_and_flush(kernel::cpu::arch::GDTR* gdtr);

#define GDT_ACCESS_TSS 0x09  // 64-bit TSS (Available)

#define GDT_ACCESS_ACCESSED   (1 << 0)
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
void GDTManager::setup_gdt() {
    // Lambda encodes a flat GDT entry; centralizing the bit layout here
    // keeps the rest of the code free from descriptor-level details.
    auto encode_entry = [&](int idx, uint64_t base, uint64_t limit, uint8_t access, uint8_t flags) {
        this->gdt[idx].base_low    = (base & 0xFFFF);
        this->gdt[idx].base_middle = (base >> 16) & 0xFF;
        this->gdt[idx].base_high   = (base >> 24) & 0xFF;
        this->gdt[idx].limit_low   = (limit & 0xFFFF);
        this->gdt[idx].granularity = ((limit >> 16) & 0x0F) | (flags & 0xF0);
        this->gdt[idx].access      = access;
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

    // 3: User Data
    encode_entry(3, 0, 0,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_SEGMENT | GDT_ACCESS_READWRITE | GDT_ACCESS_RING3,
                 GDT_FLAG_PAGE_GRAN);

    // 4: User Code
    encode_entry(4, 0, 0,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_SEGMENT | GDT_ACCESS_READWRITE |
                     GDT_ACCESS_EXECUTABLE | GDT_ACCESS_RING3,
                 GDT_FLAG_LONG_MODE | GDT_FLAG_PAGE_GRAN);

    uint64_t tss_base  = reinterpret_cast<uint64_t>(&this->tss_block);
    uint64_t tss_limit = sizeof(TSSBlock) - 1;

    // 5: TSS Descriptor
    TSSDescriptor* tss_desc = reinterpret_cast<TSSDescriptor*>(&this->gdt[5]);
    tss_desc->limit_low     = (tss_limit & 0xFFFF);
    tss_desc->base_low      = (tss_base & 0xFFFF);
    tss_desc->base_middle   = (tss_base >> 16) & 0xFF;
    tss_desc->access        = GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_TSS;
    tss_desc->granularity   = (tss_limit >> 16) & 0x0F;
    tss_desc->base_high     = (tss_base >> 24) & 0xFF;
    tss_desc->base_upper    = (tss_base >> 32);
    tss_desc->reserved      = 0;
}

void GDTManager::setup_tss(uintptr_t stack_top) {
    TSSBlock& block = this->tss_block;

    // Point IOMAP base to the array immediately following the header
    block.header.iomap_base = sizeof(TSS64);
    block.header.rsp[0]     = stack_top;

    memset(block.iopb, 0xFF, IOPB_SIZE);
    block.terminator = 0xFF;
}

void GDTManager::load_tables() {
    GDTR gdtr  = {};
    gdtr.base  = reinterpret_cast<uint64_t>(this->gdt);
    gdtr.limit = sizeof(this->gdt) - 1;

    // Install per-CPU GDT and reload segment registers in asm stub.
    load_gdt_and_flush(&gdtr);

    // Index 5 * 8 = 0x28
    asm volatile("mov $0x28, %%ax; ltr %%ax" ::: "ax");

    // LOG_INFO("GDT: loaded per-CPU tables gdt=0x%lx tss=0x%lx", gdtr.base,
    //          reinterpret_cast<uint64_t>(&this->tss_block));
}

void GDTManager::set_io_perm(uint16_t port, bool enable) {
    if (enable) {
        this->tss_block.iopb[port / 8] &= ~(1 << (port % 8));
    } else {
        this->tss_block.iopb[port / 8] |= (1 << (port % 8));
    }

    LOG_DEBUG("GDT: I/O perm %s port=0x%x", enable ? "allow" : "deny", port);
}

void GDTManager::set_ist(int ist, uintptr_t addr) {
    this->tss_block.header.ist[ist] = addr;
}
}  // namespace kernel::cpu::arch