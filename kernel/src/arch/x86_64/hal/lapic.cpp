#include "hal/lapic.hpp"
#include "cpu/features.hpp"
#include "cpu/registers.hpp"
#include "internal/lapic.h"
#include "memory/memory.hpp"
#include "libs/log.hpp"

namespace kernel::hal {
MMIORegion Lapic::lapic_base = MMIORegion();
bool Lapic::x2apic_active = false;
bool Lapic::tsc_deadline_supported = false;
uint32_t Lapic::ticks_per_ms = 0;
uint32_t Lapic::ticks_per_us = 0;
bool Lapic::is_calibrated = false;

/**
 * @brief Read a LAPIC register, abstracting xAPIC vs x2APIC access.
 *
 * Why:
 *  - xAPIC uses an MMIO window, x2APIC uses MSRs. Hiding this choice
 *    keeps the rest of the HAL code independent of the APIC mode.
 */
uint32_t Lapic::read(uint32_t offset) {
    if (x2apic_active) {
        uint32_t index = X2APIC_MSR_BASE + (offset >> 4);
        return static_cast<uint32_t>(arch::Msr::read(index).value);
    }

    return lapic_base.read<uint32_t>(offset / 4);
}

// NOLINTNEXTLINE
void Lapic::write(uint32_t offset, uint32_t val) {
    if (x2apic_active) {
        arch::Msr msr;
        msr.index = X2APIC_MSR_BASE + (offset >> 4);
        msr.value = val;
        msr.write();

        return;
    }

    lapic_base.write(offset / 4, val);
}

/**
 * @brief Initialize the local APIC in either xAPIC or x2APIC mode.
 *
 * Policy:
 *  - Prefer x2APIC when hardware supports it (cheaper IPIs, no MMIO).
 *  - Otherwise fall back to xAPIC with a single MMIO mapping.
 *  - Mask LINT0 and route LINT1 as NMI, and clear error state, so
 *    interrupts start from a known configuration.
 */
void Lapic::init() {
    x2apic_active          = arch::check_feature(FEATURE_X2APIC);
    tsc_deadline_supported = arch::check_feature(FEATURE_TSC_DEADLINE);

    arch::Msr msr = arch::Msr::read(APIC_BASE_MSR);

    if (x2apic_active) {
        msr.value |= (APIC_ENABLE_BIT | X2APIC_ENABLE_BIT);
        LOG_INFO("LAPIC: enabling x2APIC mode");
    } else {
        msr.value |= APIC_ENABLE_BIT;
        msr.value &= ~X2APIC_ENABLE_BIT;

        uintptr_t phys_base = msr.value & 0xFFFFF000;

        if (!lapic_base.ptr()) {
            // Map the LAPIC MMIO window once; all xAPIC accesses go through it.
            lapic_base = MMIORegion(phys_base, PAGE_SIZE_4K);
            LOG_INFO("LAPIC: using xAPIC MMIO base=0x%lx", phys_base);
        }
    }

    msr.write();

    // Spurious vector & LVT Masking
    write(LAPIC_SVR, 0xFF | APIC_SVR_ENABLE);
    write(LAPIC_LVT_LINT0, APIC_LVT_MASKED);
    write(LAPIC_LVT_LINT1, APIC_DELIVERY_NMI);

    // Clear errors
    write(LAPIC_ESR, 0);
    write(LAPIC_EOI, 0);
    write(LAPIC_TPR, 0);

    LOG_INFO("LAPIC: initialized (x2apic=%d, tsc-deadline=%d)",
             x2apic_active, tsc_deadline_supported);
}

/**
 * @brief Configure the LAPIC timer in a selected mode.
 *
 * Why:
 *  - Provides a uniform interface over hardware that supports legacy
 *    periodic timers and modern TSC-deadline timers, falling back to
 *    one-shot if deadline mode is unavailable.
 */
void Lapic::configure_timer(uint8_t vector, TimerMode mode) {
    stop_timer();

    if (mode != TimerMode::TSCDeadline) {
        // Set Divider
        write(LAPIC_TIMER_DIV, 0x3);
    }

    uint32_t lvt_val = vector;

    switch (mode) {
        case TimerMode::OneShort:
            lvt_val |= APIC_TIMER_ONESHOT;
            break;
        case TimerMode::Periodic:
            lvt_val |= APIC_TIMER_PERIODIC;
            break;
        case TimerMode::TSCDeadline:
            if (tsc_deadline_supported) {
                lvt_val |= APIC_TIMER_TSC_DEADLINE;
            } else {
                lvt_val |= APIC_TIMER_ONESHOT;
            }
            break;
    }

    write(LAPIC_LVT_TIMER, lvt_val);
}

void Lapic::start_timer_legacy(uint32_t count) {
    write(LAPIC_TIMER_INIT, count);
}

void Lapic::arm_ts_deadline(uint64_t target_tsc) {
    arch::Msr msr;
    msr.index = TSC_DEADLINE_MSR;
    msr.value = target_tsc;

    asm volatile("mfence" ::: "memory");
    msr.write();
}

void Lapic::stop_timer() {
    write(LAPIC_TIMER_INIT, 0);

    if (tsc_deadline_supported) {
        arch::Msr msr;
        msr.index = TSC_DEADLINE_MSR;
        msr.value = 0;
        msr.write();
    }
}

/**
 * @brief Record calibration data for LAPIC timer delays.
 *
 * The measured tick count is interpreted relative to a known time
 * interval (typically 10ms) so that later micro/millisecond delays can
 * be approximated with LAPIC counts instead of pure spin loops.
 */
void Lapic::calibrate(uint32_t measured_ticks) {
    ticks_per_ms = measured_ticks / 10;
    ticks_per_us = measured_ticks / 1000;

    if (ticks_per_us == 0) {
        ticks_per_us = 1;
    }

    is_calibrated = true;
    LOG_INFO("LAPIC: calibrated ticks_per_ms=%u ticks_per_us=%u",
             ticks_per_ms, ticks_per_us);
}

void Lapic::udelay(uint32_t us) {
    if (!is_calibrated) {
        // Fallback spinloop
        // Approximation for 2GHz CPU: 2000 cycles per us.
        // 'pause' takes ~40-100 cycles
        for (volatile uint32_t i = 0; i < us * 50; ++i) {
            asm volatile("pause");
        }

        return;
    }

    // Save current LVT config for restoring
    uint32_t prev_lvt  = read(LAPIC_LVT_TIMER);
    uint32_t prev_div  = read(LAPIC_TIMER_DIV);
    uint32_t prev_init = read(LAPIC_TIMER_INIT);

    // Setup oneshot
    write(LAPIC_TIMER_DIV, 0x3);  // Div 16
    write(LAPIC_LVT_TIMER, APIC_TIMER_ONESHOT | APIC_LVT_MASKED);

    // Set count
    uint32_t ticks = us * ticks_per_us;
    write(LAPIC_TIMER_INIT, ticks);

    // Spin wait
    while (read(LAPIC_TIMER_CUR) > 0) {
        asm volatile("pause");
    }

    // Restore timer config
    write(LAPIC_LVT_TIMER, prev_lvt);
    write(LAPIC_TIMER_DIV, prev_div);

    // If previous timer was active, restart using the old init value
    if (!(prev_lvt & APIC_LVT_MASKED)) {
        write(LAPIC_TIMER_INIT, prev_init);
    }
}

void Lapic::mdelay(uint32_t ms) {
    for (uint32_t i = 0; i < ms; ++i) {
        udelay(1000);
    }
}

uint32_t Lapic::get_id() {
    uint32_t val = read(LAPIC_ID);

    if (x2apic_active) {
        return val;
    } else {
        return val >> 24;
    }
}

void Lapic::send_eoi() {
    write(LAPIC_EOI, 0);
}

// NOLINTNEXTLINE
void Lapic::send_ipi(uint32_t dest_id, uint8_t vector) {
    if (x2apic_active) {
        arch::Msr msr;
        msr.index = X2APIC_MSR_BASE + (LAPIC_ICR_LOW >> 4);
        msr.value = (static_cast<uint64_t>(dest_id) << 32) | vector | APIC_DELIVERY_FIXED;
        msr.write();
    } else {
        write(LAPIC_ICR_HIGH, dest_id << 24);
        write(LAPIC_ICR_LOW, vector | APIC_DELIVERY_FIXED);
    }
}

void Lapic::broadcast_ipi(uint8_t vector) {
    if(x2apic_active) {
        // Destination: 0xFFFFFFFF (targets all CPUs)
        // Mode: Logical
        arch::Msr msr;
        msr.index = X2APIC_MSR_BASE + (LAPIC_ICR_LOW >> 4);
        msr.value = (static_cast<uint64_t>(0xFFFFFFFF) << 32) | vector | APIC_DELIVERY_FIXED | APIC_DEST_LOGICAL;
        msr.write();
    } else {
        write(LAPIC_ICR_LOW, vector | 0xC0000 | APIC_DELIVERY_FIXED);
    }
}

// NOLINTNEXTLINE
void Lapic::send_init_sipi(uint32_t dest_id, uint8_t page) {
    if (x2apic_active) {
        arch::Msr msr;
        msr.index = X2APIC_MSR_BASE + (LAPIC_ICR_LOW >> 4);
        msr.value = (static_cast<uint64_t>(dest_id) << 32) | APIC_DELIVERY_INIT;
        msr.write();
    } else {
        write(LAPIC_ICR_HIGH, dest_id << 24);
        write(LAPIC_ICR_LOW, APIC_DELIVERY_INIT);
    }

    // Wait 10ms
    mdelay(10);

    if (x2apic_active) {
        arch::Msr msr;
        msr.index = X2APIC_MSR_BASE + (LAPIC_ICR_LOW >> 4);
        msr.value = (static_cast<uint64_t>(dest_id) << 32) | APIC_DELIVERY_START | page;
        msr.write();
    } else {
        write(LAPIC_ICR_HIGH, dest_id << 24);
        write(LAPIC_ICR_LOW, APIC_DELIVERY_START | page);
    }

    // Wait 200us
    udelay(200);

    if (x2apic_active) {
        arch::Msr msr;
        msr.index = X2APIC_MSR_BASE + (LAPIC_ICR_LOW >> 4);
        msr.value = (static_cast<uint64_t>(dest_id) << 32) | APIC_DELIVERY_START | page;
        msr.write();
    } else {
        write(LAPIC_ICR_HIGH, dest_id << 24);
        write(LAPIC_ICR_LOW, APIC_DELIVERY_START | page);
    }
}
}  // namespace kernel::hal