#pragma once

// General Registers (0x00 - 0xF0)
#define HPET_CAPS_ID_REG    0x00  // General Capabilities & ID
#define HPET_CONFIG_REG     0x10  // General Configuration
#define HPET_INT_STATUS_REG 0x20  // General Interrupt Status
#define HPET_COUNTER_REG    0xF0  // Main Counter Value

// General Config Bits
#define HPET_CFG_ENABLE (1ul << 0)
#define HPET_CFG_LEGACY (1ul << 1)

// General Caps Bits
#define HPET_CAPS_COUNT_SIZE      (1ul << 13)  // 1 64-bit
#define HPET_CAPS_NUM_TIMERS_MASK 0x1F00     // Bits 8-12

// Timer N Registers
#define HPET_Tn_REG_START 0x100
#define HPET_Tn_REG_STEP 0x20

// N Timer Index (0..31)
// Offset 0x100 + (N * 0x20)
#define HPET_Tn_CFG_OFFSET 0x00  // Config & Capabilities
#define HPET_Tn_CMP_OFFSET 0x08  // Comparator Value
#define HPET_Tn_FSB_OFFSET 0x10  // FSB Route (MSI)

// Timer Config Register Bits
#define HPET_Tn_INT_TYPE_LEVEL (1ul << 1)            // 0=Edge, 1=Level
#define HPET_Tn_ENABLE         (1ul << 2)            // Interrupt Enable
#define HPET_Tn_TYPE_PERIODIC  (1ul << 3)            // 0=OneShot, 1=Periodic
#define HPET_Tn_PER_INT_CAP    (1ul << 4)            // Periodic Capable (RO)
#define HPET_Tn_SIZE_64        (1ul << 5)            // 64-bit Capable (RO)
#define HPET_Tn_VAL_SET        (1ul << 6)            // Set Accumulator (Write 1)
#define HPET_Tn_32MODE         (1ul << 8)            // Force 32-bit mode
#define HPET_Tn_INT_ROUTE_MASK 0xFFFFFFFF00000000  // IRQ Routing Bits
#define HPET_Tn_INT_ROUTE_SHIFT 32