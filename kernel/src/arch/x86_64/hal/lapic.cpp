#include "hal/lapic.hpp"
#include "arch.hpp"
#include "cpu/features.hpp"
#include "cpu/registers.hpp"
#include "internal/lapic.h"
#include "memory/memory.hpp"
#include "hal/pit.hpp"
#include "hal/hpet.hpp"
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
void pit_wait_10ms() {
    constexpr uint16_t TARGET_PIT_TICKS = 11932;

    uint16_t pit_start = PIT::read_count();

    while (true) {
        uint16_t pit_now = PIT::read_count();

        uint16_t delta = pit_start - pit_now;

        if (delta >= TARGET_PIT_TICKS) {
            break;  // 10 ms passed!
        }

        arch::pause();
    }
}

void hpet_wait_10ms() {
    HPET::mdelay(10);
}
}  // namespace

size_t Lapic::rdtsc() {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<size_t>(hi) << 32) | lo;
}

uint32_t Lapic::read(uint32_t offset) {
    if (x2apic_active) {
        uint32_t index = X2APIC_MSR_BASE + (offset >> 4);
        return static_cast<uint32_t>(arch::Msr::read(index).value);
    } else {
        return lapic_base.read<uint32_t>(offset / 4);
    }
}

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

void Lapic::init() {
    x2apic_active          = arch::check_feature(FEATURE_X2APIC);
    tsc_deadline_supported = arch::check_feature(FEATURE_TSC_DEADLINE);

    arch::Msr msr = arch::Msr::read(APIC_BASE_MSR);

    if (x2apic_active) {
        msr.value |= (APIC_ENABLE_BIT | X2APIC_ENABLE_BIT);
        // LOG_INFO("LAPIC: enabling x2APIC mode");
    } else {
        msr.value |= APIC_ENABLE_BIT;
        msr.value &= ~X2APIC_ENABLE_BIT;

        uintptr_t phys_base = msr.value & 0xFFFFF000;

        if (!lapic_base.ptr()) {
            // Map the LAPIC MMIO window once; all xAPIC accesses go through it.
            lapic_base = MMIORegion(phys_base, memory::PAGE_SIZE_4K);
            // LOG_INFO("LAPIC: using xAPIC MMIO base=0x%lx", phys_base);
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

    // LOG_INFO("LAPIC: initialized (x2apic=%d, tsc-deadline=%d)", x2apic_active,
    //  tsc_deadline_supported);
}

void Lapic::configure_timer(uint8_t vector, TimerMode mode) {
    stop_timer();

    if (mode != TimerMode::TscDeadline) {
        // For non-deadline modes, we use a fixed divider (16).
        write(LAPIC_TIMER_DIV, 0x3);
    }

    uint32_t lvt_val = vector;

    switch (mode) {
        case TimerMode::OneShot:
            lvt_val |= APIC_TIMER_ONESHOT;
            break;
        case TimerMode::Periodic:
            lvt_val |= APIC_TIMER_PERIODIC;
            break;
        case TimerMode::TscDeadline:
            if (tsc_deadline_supported) {
                lvt_val |= APIC_TIMER_TSC_DEADLINE;
            } else {
                // Graceful degradation when the CPU lacks deadline mode.
                lvt_val |= APIC_TIMER_ONESHOT;
            }
            break;
    }

    write(LAPIC_LVT_TIMER, lvt_val);
}

void Lapic::start_timer(uint32_t count) {
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

void Lapic::calibrate_with_pit() {
    constexpr uint32_t CALIBRATION_MS = 10;
    PIT::prepare_wait(CALIBRATION_MS);

    perform_calibration_race(pit_wait_10ms);

    PIT::disable();
    // LOG_INFO("LAPIC: calibrated using PIT as 10ms reference");
}

void Lapic::calibrate_with_hpet() {
    perform_calibration_race(hpet_wait_10ms);
    // LOG_INFO("LAPIC: calibrated using HPET as 10ms reference");
}

void Lapic::perform_calibration_race(void (*callback)()) {
    bool int_enabled = false;
    if (arch::interrupt_status()) {
        int_enabled = true;
        arch::disable_interrupts();
    }

    // Program LAPIC timer as a large one-shot countdown.
    write(LAPIC_TIMER_DIV, 0x3);
    write(LAPIC_LVT_TIMER, APIC_TIMER_ONESHOT | APIC_LVT_MASKED);
    write(LAPIC_TIMER_INIT, 0xFFFFFFFF);

    asm volatile("lfence");
    size_t tsc_start = rdtsc();

    // External reference (PIT or HPET) waits ~10ms.
    callback();

    asm volatile("lfence");
    size_t tsc_end = rdtsc();

    uint32_t apic_end = read(LAPIC_TIMER_CUR);

    write(LAPIC_TIMER_INIT, 0);

    if (int_enabled) {
        arch::enable_interrupts();
    }

    uint32_t apic_ticks_total = 0xFFFFFFFF - apic_end;
    size_t tsc_ticks_total    = tsc_end - tsc_start;

    // Normalize both counters to "per millisecond" units.
    ticks_per_ms = apic_ticks_total / 10;
    tsc_per_ms   = tsc_ticks_total / 10;

    ticks_per_us = ticks_per_ms / 1000;

    if (ticks_per_us == 0) {
        // Avoid division by zero in delay helpers; keep a minimum granularity.
        ticks_per_us = 1;
    }

    if (ticks_per_ms < 1000) {
        // Very low tick rates tend to produce poor short delays; clamp.
        ticks_per_ms = 1000;
    }

    is_calibrated = true;
}

void Lapic::calibrate() {
    // Preference order:
    //  1. Modern CPUID leaf 0x15 (if it exposes usable TSC/Crystal info).
    //  2. HPET-based 10ms measurement.
    //  3. PIT-based 10ms measurement.
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
            // LOG_INFO(
            // "LAPIC: calibrated from CPUID (ticks_per_ms=%u ticks_per_us=%u tsc_per_ms=%lu)",
            // ticks_per_ms, ticks_per_us, tsc_per_ms);
            return;
        }
    }

    // If modern CPUID-based calibration is not available or insufficient,
    // fall back to external 10ms references (HPET preferred over PIT).
    if (HPET::is_available()) {
        calibrate_with_hpet();
    } else {
        calibrate_with_pit();
    }

    // LOG_INFO("LAPIC: calibrated ticks_per_ms=%u ticks_per_us=%u tsc_per_ms=%lu", ticks_per_ms,
    //  ticks_per_us, tsc_per_ms);
}

void Lapic::udelay(uint32_t us) {
    if (!is_calibrated) {
        // Fallback spinloop when no timebase is calibrated yet.
        // Approximation for ~2GHz CPU: ~2000 cycles per µs; 'pause' is
        // cheap and keeps SMT siblings happier.
        for (volatile uint32_t i = 0; i < us * 50; i += 1) {
            arch::pause();
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

    // Convert microseconds to LAPIC ticks.
    uint64_t ticks = (static_cast<uint64_t>(us) * ticks_per_ms) / 1000;
    write(LAPIC_TIMER_INIT, static_cast<uint32_t>(ticks));

    // Spin wait
    while (read(LAPIC_TIMER_CUR) > 0) {
        arch::pause();
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
    if (!is_calibrated) {
        // Crude early-boot fallback; good enough before the LAPIC timer
        // has a meaningful scale.
        for (volatile uint32_t i = 0; i < ms * 10000; i += 1) {
            arch::pause();
        }

        return;
    }

    uint64_t total_ticks = static_cast<uint64_t>(ms) * ticks_per_ms;

    uint32_t prev_lvt  = read(LAPIC_LVT_TIMER);
    uint32_t prev_div  = read(LAPIC_TIMER_DIV);
    uint32_t prev_init = read(LAPIC_TIMER_INIT);

    write(LAPIC_TIMER_DIV, 0x3);
    write(LAPIC_LVT_TIMER, APIC_TIMER_ONESHOT | APIC_LVT_MASKED);

    // If the delay exceeds the 32-bit counter range, split into chunks.
    while (total_ticks > 0xFFFFFFFF) {
        write(LAPIC_TIMER_INIT, 0xFFFFFFFF);

        while (read(LAPIC_TIMER_CUR) > 0) {
            arch::pause();
        }

        total_ticks -= 0xFFFFFFFF;
    }

    if (total_ticks > 0) {
        write(LAPIC_TIMER_INIT, static_cast<uint32_t>(total_ticks));

        while (read(LAPIC_TIMER_CUR) > 0) {
            arch::pause();
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
        // x2APIC exposes the full APIC ID directly in the register.
        return val;
    } else {
        // xAPIC encodes ID in the high 8 bits.
        return val >> 24;
    }
}

void Lapic::send_eoi() {
    write(LAPIC_EOI, 0);
}

void Lapic::send_ipi(uint32_t dest_id, uint8_t vector) {
    // Wait for delivery status to be Idle; While strictly not
    // required for x2APIC, it's a good practice for older xAPIC.
    while (read(LAPIC_ICR_LOW) & APIC_DELIVERY_STATUS) {
        arch::pause();
    }

    if (x2apic_active) {
        arch::Msr msr;
        msr.index = X2APIC_MSR_BASE + (LAPIC_ICR_LOW >> 4);
        msr.value = (static_cast<uint64_t>(dest_id) << 32) | vector | APIC_DELIVERY_FIXED |
                    APIC_DELIVERY_ASSERT | APIC_EDGE_TRIGGER;
        msr.write();
    } else {
        write(LAPIC_ICR_HIGH, dest_id << 24);
        write(LAPIC_ICR_LOW, vector | APIC_DELIVERY_FIXED | APIC_DELIVERY_ASSERT);
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

void Lapic::broadcast_ipi(uint8_t vector, bool self) {
    while (read(LAPIC_ICR_LOW) & APIC_DELIVERY_STATUS) {
        arch::pause();
    }

    uint32_t icr_val = vector;
    icr_val |= APIC_DELIVERY_FIXED | APIC_DELIVERY_ASSERT | APIC_EDGE_TRIGGER;

    if (self) {
        icr_val |= ICR_DEST_ALL_INC_SELF;
    } else {
        icr_val |= ICR_DEST_ALL_EXC_SELF;
    }

    if (x2apic_active) {
        // Destination: 0xFFFFFFFF (targets all CPUs)
        // Mode: Logical
        arch::Msr msr;
        msr.index = X2APIC_MSR_BASE + (LAPIC_ICR_LOW >> 4);
        msr.value = icr_val;
        msr.write();
    } else {
        write(LAPIC_ICR_LOW, icr_val);
    }
}

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

    // First INIT-SIPI sequence: spec recommends a small delay between INIT
    // and the first SIPI to give the APs time to reset.
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

    // Second SIPI after a short delay per Intel recommendations.
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
        // Lack of calibration means no meaningful absolute time yet.
        return 0;
    }

    uint64_t now = rdtsc();

    // Convert TSC ticks to nanoseconds using the per-ms calibration.
    return (now * 1000000) / ticks_per_ms;
}
}  // namespace kernel::hal