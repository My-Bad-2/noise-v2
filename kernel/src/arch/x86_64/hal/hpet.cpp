#include "hal/hpet.hpp"
#include "memory/memory.hpp"
#include "uacpi/tables.h"
#include "uacpi/acpi.h"
#include "internal/hpet.h"
#include "arch.hpp"
#include "uacpi/types.h"
#include "libs/log.hpp"

namespace kernel::hal {
MMIORegion HPET::hpet_base = MMIORegion();
uint32_t HPET::period_fs   = 0;
uint8_t HPET::num_timers   = 0;
bool HPET::available       = false;

// NOLINTNEXTLINE
void HPET::write(size_t reg, size_t val) {
    if (!hpet_base.ptr()) {
        return;
    }
    
    hpet_base.write(reg, val);
}

size_t HPET::read(size_t reg) {
    if (!hpet_base.ptr()) {
        return 0;
    }

    return hpet_base.read<size_t>(reg);
}

// NOLINTNEXTLINE
void HPET::write_timer(uint8_t index, size_t offset, size_t val) {
    size_t reg = HPET_Tn_REG_START + (index * HPET_Tn_REG_STEP) + offset;
    write(reg, val);
}

size_t HPET::read_timer(uint8_t index, size_t offset) {
    size_t reg = HPET_Tn_REG_START + (index * HPET_Tn_REG_STEP) + offset;
    return read(reg);
}

void HPET::init() {
    uacpi_table hpet_table;
    uacpi_status ret = uacpi_table_find_by_signature(ACPI_HPET_SIGNATURE, &hpet_table);

    if (ret != UACPI_STATUS_OK) {
        // No HPET table => platform either doesn't have HPET or firmware
        // chose not to expose it. We silently fall back to PIT/LAPIC.
        return;
    }

    acpi_hpet* tbl = reinterpret_cast<acpi_hpet*>(hpet_table.ptr);

    if (tbl->address.address_space_id != UACPI_ADDRESS_SPACE_SYSTEM_MEMORY) {
        // Some exotic systems might use non-memory HPET; we only support
        // standard system-memory mappings here.
        uacpi_table_unref(&hpet_table);
        LOG_WARN("HPET: unsupported address space id=%u", tbl->address.address_space_id);
        return;
    }

    uintptr_t phys_addr = tbl->address.address;
    uacpi_table_unref(&hpet_table);

    hpet_base = MMIORegion(phys_addr, PAGE_SIZE_4K);

    size_t caps        = read(HPET_CAPS_ID_REG);
    uint16_t vendor_id = (caps >> 16) & 0xFFFF;
    period_fs          = caps >> 32;

    if (vendor_id == 0 || vendor_id == 0xFFFF) {
        available = false;
        LOG_WARN("HPET: invalid vendor id=0x%x", vendor_id);
        return;
    }

    // Sanity-check period; values of 0 or very large periods are unusable.
    if (period_fs == 0 || period_fs > 1000000000) {
        available = false;
        LOG_WARN("HPET: invalid period_fs=%u, disabling HPET", period_fs);
        return;
    }

    num_timers = ((caps & HPET_CAPS_NUM_TIMERS_MASK) >> 8) + 1;

    uint64_t cfg = read(HPET_CONFIG_REG);
    cfg |= HPET_CFG_ENABLE;
    cfg &= ~HPET_CFG_LEGACY;  // Avoid legacy IRQ routing.

    write(HPET_CONFIG_REG, cfg);

    uint64_t cfg_check = read(HPET_CONFIG_REG);

    if (!(cfg_check & HPET_CFG_ENABLE)) {
        available = false;
        LOG_WARN("HPET: Could not enabled! Config readback: 0x%lx", cfg_check);
        return;
    }

    uint64_t start_value = read(HPET_COUNTER_REG);
    int timeout          = 10000;

    while (read(HPET_COUNTER_REG) == start_value) {
        arch::pause();
        timeout--;
        if (timeout == 0) {
            available = false;
            LOG_WARN("HPET: Counter stuck at 0x%lx", start_value);
            return;
        }
    }

    available = true;
    LOG_INFO("HPET: initialized at phys=0x%lx period_fs=%u timers=%u", phys_addr, period_fs,
             num_timers);
}

size_t HPET::read_counter() {
    return read(HPET_COUNTER_REG);
}

size_t HPET::get_ns() {
    if (!available) {
        // Callers should treat 0 as "no HPET time available".
        return 0;
    }

    size_t ticks = read_counter();

    // Convert (ticks * period_fs) from femtoseconds to nanoseconds.
    unsigned __int128 total_fs = static_cast<unsigned __int128>(ticks) * period_fs;
    return static_cast<uint64_t>(total_fs / 1000000);
}

void HPET::ndelay(size_t ns) {
    if (!available) {
        // If HPET is missing, higher layers are expected to fall back
        // to PIT/LAPIC-based delays; we do nothing here.
        return;
    }

    size_t start                = read_counter();
    unsigned __int128 target_fs = static_cast<unsigned __int128>(ns) * period_fs;
    size_t ticks_needed         = static_cast<size_t>(target_fs / period_fs);

    while (read_counter() < ticks_needed) {
        arch::pause();
    }
}

void HPET::udelay(size_t us) {
    ndelay(us * 1000);
}

void HPET::mdelay(size_t ms) {
    ndelay(ms * 1000000);
}

// NOLINTNEXTLINE
bool HPET::enable_periodic_timer(uint8_t timer_idx, size_t hz, uint8_t irq_gsi) {
    if (!available || (timer_idx >= num_timers) || (hz == 0)) {
        LOG_WARN("HPET: periodic timer setup failed (available=%d idx=%u hz=%zu)", available,
                 timer_idx, hz);
        return false;
    }

    size_t t_caps = read_timer(timer_idx, HPET_Tn_CFG_OFFSET);

    if (!(t_caps & HPET_Tn_PER_INT_CAP)) {
        LOG_WARN("HPET: timer %u does not support periodic mode", timer_idx);
        return false;
    }

    uint64_t cfg = t_caps;
    cfg &= ~HPET_Tn_ENABLE;
    cfg &= ~HPET_Tn_INT_TYPE_LEVEL;
    write_timer(timer_idx, HPET_Tn_CFG_OFFSET, cfg);

    cfg |= static_cast<uint64_t>(irq_gsi) << HPET_Tn_INT_ROUTE_SHIFT;

    // Compute tick interval from desired Hz using femtosecond period.
    unsigned __int128 fs_per_sec      = 1000000000000000ULL;
    unsigned __int128 period_total_fs = fs_per_sec / hz;
    size_t tick_delta                 = static_cast<size_t>(period_total_fs / period_fs);

    cfg |= HPET_Tn_TYPE_PERIODIC | HPET_Tn_VAL_SET;

    if (t_caps & HPET_Tn_SIZE_64) {
        cfg |= HPET_Tn_SIZE_64;
    } else {
        cfg |= HPET_Tn_32MODE;
    }

    write_timer(timer_idx, HPET_Tn_CFG_OFFSET, cfg);

    size_t main_count = read_counter();
    write_timer(timer_idx, HPET_Tn_CMP_OFFSET, main_count + tick_delta);

    cfg &= ~HPET_Tn_VAL_SET;
    cfg |= HPET_Tn_ENABLE;
    write_timer(timer_idx, HPET_Tn_CFG_OFFSET, cfg);

    LOG_INFO("HPET: enabled periodic timer idx=%u hz=%zu irq_gsi=%u", timer_idx, hz, irq_gsi);
    return true;
}

// NOLINTNEXTLINE
bool HPET::enable_oneshot_timer(uint8_t timer_idx, size_t us_delay, uint8_t irq_gsi) {
    if (!available || (timer_idx >= num_timers)) {
        LOG_WARN("HPET: one-shot timer setup failed (available=%d idx=%u)", available, timer_idx);
        return false;
    }

    size_t t_caps = read_timer(timer_idx, HPET_Tn_CFG_OFFSET);

    uint64_t cfg = t_caps & ~HPET_Tn_ENABLE;
    cfg &= ~HPET_Tn_TYPE_PERIODIC;
    cfg &= ~HPET_Tn_INT_TYPE_LEVEL;
    cfg &= ~HPET_Tn_INT_ROUTE_MASK;
    cfg |= (static_cast<uint64_t>(irq_gsi) << HPET_Tn_INT_ROUTE_SHIFT);

    write_timer(timer_idx, HPET_Tn_CFG_OFFSET, cfg);

    // Convert microseconds to femtoseconds, then to ticks.
    unsigned __int128 target_fs = static_cast<unsigned __int128>(us_delay) * 1000000000;
    size_t ticks                = static_cast<size_t>(target_fs / period_fs);
    size_t curr                 = read_counter();
    size_t match                = curr + ticks;

    write_timer(timer_idx, HPET_Tn_CMP_OFFSET, match);
    cfg |= HPET_Tn_ENABLE;
    write_timer(timer_idx, HPET_Tn_CFG_OFFSET, cfg);

    LOG_DEBUG("HPET: enabled one-shot timer idx=%u delay_us=%zu irq_gsi=%u", timer_idx, us_delay,
              irq_gsi);
    return true;
}
}  // namespace kernel::hal