#pragma once

// Memory Mapped Registers (Offsets from Base)
#define IOAPIC_REGSEL 0x00  // Index Register
#define IOAPIC_IOWIN  0x10  // Data Register

// Internal Indexes (Accessed via REGSEL)
#define IOAPIC_ID     0x00
#define IOAPIC_VER    0x01
#define IOAPIC_ARB    0x02
#define IOAPIC_REDTBL 0x10  // Redirection Table entries start here

#define IOAPIC_STATUS_PENDING 0x1000