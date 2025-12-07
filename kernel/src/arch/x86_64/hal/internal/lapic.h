#pragma once

#include "cpu/regs.h"

/**
 * @brief Internal LAPIC/x2APIC register and bit definitions.
 *
 * This header is private to the LAPIC implementation and mirrors the
 * architectural layout of APIC registers, LVT encodings, and MSR base
 * indices.
 */

// MSRs
#define TSC_DEADLINE_MSR 0x6E0  // Standard architectural MSR
#define APIC_BASE_MSR    0x1B
#define X2APIC_MSR_BASE  0x800

// xAPIC MMIO
#define LAPIC_DEFAULT_BASE 0xFEE00000

// CPUID Flags
#define CPUID_X2APIC_FLAG       1 << 21  // Leaf 1, ECX
#define CPUID_TSC_DEADLINE_FLAG 1 << 24  // Leaf 1, ECX

// Timer Modes (LVT Bits 17 and 18)
// Periodic (Bit 17), TSC-Deadline (Bit 18)
#define APIC_TIMER_ONESHOT      0x00000
#define APIC_TIMER_PERIODIC     0x20000
#define APIC_TIMER_TSC_DEADLINE 0x40000

// Register Offsets (xAPIC Byte Offsets)
#define LAPIC_ID         0x020
#define LAPIC_VER        0x030
#define LAPIC_TPR        0x080
#define LAPIC_EOI        0x0B0
#define LAPIC_LDR        0x0D0
#define LAPIC_DFR        0x0E0
#define LAPIC_SVR        0x0F0
#define LAPIC_ESR        0x280
#define LAPIC_ICR_LOW    0x300
#define LAPIC_ICR_HIGH   0x310
#define LAPIC_LVT_TIMER  0x320
#define LAPIC_LVT_LINT0  0x350
#define LAPIC_LVT_LINT1  0x360
#define LAPIC_LVT_ERROR  0x370
#define LAPIC_TIMER_INIT 0x380
#define LAPIC_TIMER_CUR  0x390
#define LAPIC_TIMER_DIV  0x3E0

// Flags & Constants
#define APIC_ENABLE_BIT   (1 << 11)
#define X2APIC_ENABLE_BIT (1 << 10)

#define APIC_DELIVERY_FIXED 0x000
#define APIC_DELIVERY_NMI   0x400
#define APIC_DELIVERY_INIT  0x500
#define APIC_DELIVERY_START 0x600

#define APIC_LVT_MASKED     0x10000
#define APIC_TIMER_PERIODIC 0x20000
#define APIC_SVR_ENABLE     0x100

#define APIC_EDGE_TRIGGER  0x000
#define APIC_LEVEL_TRIGGER 0x8000

#define APIC_DEST_LOGICAL 0x800