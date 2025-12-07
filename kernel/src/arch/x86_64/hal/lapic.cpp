#include "hal/lapic.hpp"
#include "arch.hpp"
#include "cpu/features.hpp"
#include "cpu/registers.hpp"
#include "internal/lapic.h"
#include "memory/memory.hpp"
#include "libs/log.hpp"
#include "hal/pit.hpp"
#include <cpuid.h>

namespace kernel::hal {
MMIORegion Lapic::lapic_base       = MMIORegion();
bool Lapic::x2apic_active          = false;
bool Lapic::tsc_deadline_supported = false;
uint32_t Lapic::ticks_per_ms       = 0;
uint32_t Lapic::ticks_per_us       = 0;
bool Lapic::is_calibrated          = false;
uint64_t Lapic::tsc_per_ms         = 0;

namespace {
/**
 * @brief Read the current timestamp counter (TSC).
 */
size_t rdtsc() {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<size_t>(hi) << 32) | lo;
}
}  // namespace

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
    } else {
        return lapic_base.read<uint32_t>(offset / 4);
    }
}

// NOLINTNEXTLINE
void Lapic::write(uint32_t offset, uint32_t val) {
    if (x2apic_active) {
        arch::Msr msr;
        msr.index = X2APIC_MSR_BASE + (offset >> 4);
        msr.value = val;
        msr.write();
    } else {
        lapic_base.write(offset / 4, val);
    }
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

    LOG_INFO("LAPIC: initialized (x2apic=%d, tsc-deadline=%d)", x2apic_active,
             tsc_deadline_supported);
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

void Lapic::arm_tsc_deadline(uint64_t target_tsc) {
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
void Lapic::calibrate() {
    uint32_t eax, ebx, ecx, edx;

    __get_cpuid(0x15, &eax, &ebx, &ecx, &edx);

    // ECX = Crystal Clock in Hz
    // EAX = Denominator
    // EBX = Numerator
    if (eax != 0 && ecx != 0) {
        // Core Frequency = (Crystal Frequency * Numerator) / Denominator
        uint64_t core_freq_hz = static_cast<uint64_t>(ecx * ebx) / eax;
        
        tsc_per_ms   = core_freq_hz / 1000;
        ticks_per_ms = ecx / 1000;
        ticks_per_us = ticks_per_ms / 1000;

        if (ticks_per_us > 0) {
            is_calibrated = true;
            LOG_INFO("LAPIC: calibrated from CPUID (ticks_per_ms=%u ticks_per_us=%u tsc_per_ms=%lu)",
                     ticks_per_ms, ticks_per_us, tsc_per_ms);
            return;
        }
    }

    // If modern CPUID-based calibration is not available or insufficient,
    // fall back to using the PIT as an external time reference for a
    // classic 10ms measurement window.
    bool int_enabled = false;

    if (arch::interrupt_status()) {
        int_enabled = true;
        arch::disable_interrupts();
    }

    constexpr uint16_t TARGET_PIT_TICKS = 11932;
    constexpr uint32_t CALIBRATION_MS = 10;
    PIT::prepare_wait(CALIBRATION_MS);

    write(LAPIC_TIMER_DIV, 0x3);
    write(LAPIC_LVT_TIMER, APIC_TIMER_ONESHOT | APIC_LVT_MASKED);

    write(LAPIC_TIMER_INIT, 0xFFFFFFFF);

    asm volatile("lfence");
    size_t tsc_start = rdtsc();

    uint16_t pit_start = PIT::read_count();

    while(true) {
        uint16_t pit_now = PIT::read_count();

        uint16_t delta = pit_start - pit_now;

        if(delta >= TARGET_PIT_TICKS) {
            break; // 10 ms passed!
        }

        asm volatile("pause");
    }

    asm volatile("lfence");
    size_t tsc_end = rdtsc();

    uint32_t apic_end = read(LAPIC_TIMER_CUR);

    PIT::disable();

    if (int_enabled) {
        arch::enable_interrupts();
    }

    uint32_t apic_ticks_total = 0xFFFFFFFF - apic_end;
    size_t tsc_ticks_total    = tsc_end - tsc_start;

    ticks_per_ms = apic_ticks_total / CALIBRATION_MS;
    tsc_per_ms   = tsc_ticks_total / CALIBRATION_MS;

    ticks_per_us = ticks_per_ms / 1000;

    if (ticks_per_us == 0) {
        ticks_per_us = 1;
    }

    if (ticks_per_ms == 0) {
        ticks_per_ms = 1000;
    }

    is_calibrated = true;
    LOG_INFO("LAPIC: calibrated ticks_per_ms=%u ticks_per_us=%u tsc_per_ms=%lu", ticks_per_ms,
             ticks_per_us, tsc_per_ms);
}

void Lapic::udelay(uint32_t us) {
    if (!is_calibrated) {
        // Fallback spinloop
        // Approximation for 2GHz CPU: 2000 cycles per us.
        // 'pause' takes ~40-100 cycles
        for (volatile uint32_t i = 0; i < us * 50; i += 1) {
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
    uint64_t ticks = (static_cast<uint64_t>(us) * ticks_per_ms) / 1000;
    write(LAPIC_TIMER_INIT, static_cast<uint32_t>(ticks));

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
    if(!is_calibrated) {
        for(volatile uint32_t i = 0; i < ms * 10000; i += 1) {
            asm volatile("pause");
        }

        return;
    }

    uint64_t total_ticks = static_cast<uint64_t>(ms) * ticks_per_ms;

    uint32_t prev_lvt = read(LAPIC_LVT_TIMER);
    uint32_t prev_div = read(LAPIC_TIMER_DIV);
    uint32_t prev_init = read(LAPIC_TIMER_INIT);

    write(LAPIC_TIMER_DIV, 0x3);
    write(LAPIC_LVT_TIMER, APIC_TIMER_ONESHOT | APIC_LVT_MASKED);

    while(total_ticks > 0xFFFFFFFF) {
        write(LAPIC_TIMER_INIT, 0xFFFFFFFF);
        
        while(read(LAPIC_TIMER_CUR) > 0) {
            asm volatile("pause");
        }

        total_ticks -= 0xFFFFFFFF;
    }

    if(total_ticks > 0) {
        write(LAPIC_TIMER_INIT, static_cast<uint32_t>(total_ticks));

        while(read(LAPIC_TIMER_CUR) > 0) {
            asm volatile("pause");
        }
    }

    write(LAPIC_LVT_TIMER, prev_lvt);
    write(LAPIC_TIMER_DIV, prev_div);

    // If previous timer was active, restart using the old init value
    if (!(prev_lvt & APIC_LVT_MASKED)) {
        write(LAPIC_TIMER_INIT, prev_init);
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
    if (x2apic_active) {
        // Destination: 0xFFFFFFFF (targets all CPUs)
        // Mode: Logical
        arch::Msr msr;
        msr.index = X2APIC_MSR_BASE + (LAPIC_ICR_LOW >> 4);
        msr.value = (static_cast<uint64_t>(0xFFFFFFFF) << 32) | vector | APIC_DELIVERY_FIXED |
                    APIC_DEST_LOGICAL;
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

uint64_t Lapic::get_ticks_ns() {
    if (!is_calibrated || ticks_per_ms == 0) {
        return 0;
    }

    uint64_t now = rdtsc();

    return (now * 1000000) / ticks_per_ms;
}
}  // namespace kernel::hal