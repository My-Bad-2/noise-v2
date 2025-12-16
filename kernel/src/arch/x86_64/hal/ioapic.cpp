#include "hal/ioapic.hpp"
#include "hal/apic.hpp"
#include "internal/ioapic.h"
#include "libs/log.hpp"

namespace kernel::hal {
IOAPIC::Controller IOAPIC::controllers[MAX_CONTROLLERS];
int IOAPIC::num_controllers = 0;
IsoInfo* IOAPIC::iso_list   = nullptr;

// NOLINTNEXTLINE
uint32_t IOAPIC::read(int idx, uint32_t reg) {
    // IOAPIC uses an index/data pair; we first select the register,
    // then read the corresponding value from IOWIN.
    controllers[idx].virt_base.write_at(IOAPIC_REGSEL, reg);
    return controllers[idx].virt_base.read_at<uint32_t>(IOAPIC_IOWIN);
}

// NOLINTNEXTLINE
void IOAPIC::write(int idx, uint32_t reg, uint32_t val) {
    controllers[idx].virt_base.write_at(IOAPIC_REGSEL, reg);
    controllers[idx].virt_base.write_at(IOAPIC_IOWIN, val);
}

int IOAPIC::get_controller_idx(uint32_t gsi) {
    // Find which IOAPIC owns this GSI by checking configured ranges.
    for (int i = 0; i < num_controllers; ++i) {
        if (gsi >= controllers[i].gsi_start && gsi <= controllers[i].gsi_end) {
            return i;
        }
    }

    return -1;
}

IsoInfo* IOAPIC::find_iso(uint8_t irq) {
    IsoInfo* node = iso_list;

    // Only ISOs for bus 0 (ISA) are considered here: they remap legacy
    // IRQ numbers to alternative GSIs and change polarity/trigger.
    while (node) {
        if (node->iso.bus == 0 && node->iso.source == irq) {
            return node;
        }

        node = node->next;
    }

    return nullptr;
}

void IOAPIC::init() {
    // Snapshot the ISO list and IOAPIC list built during ACPI parsing.
    iso_list         = IsoInfo::head();
    IoApicInfo* node = IoApicInfo::head();

    while (node && num_controllers < MAX_CONTROLLERS) {
        int idx                    = num_controllers;
        controllers[idx].id        = node->ioapic.id;
        controllers[idx].virt_base = MMIORegion(node->ioapic.address, PAGE_SIZE_4K);
        controllers[idx].gsi_start = node->ioapic.gsi_base;

        uint32_t ver   = read(idx, IOAPIC_VER);
        uint32_t count = ((ver >> 16) & 0xFF) + 1;

        controllers[idx].gsi_end = controllers[idx].gsi_start + count - 1;
        num_controllers++;

        // Start with all pins masked; individual routes will be enabled
        // explicitly by the interrupt setup code.
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t gsi = controllers[idx].gsi_start + i;
            mask_gsi(gsi);
        }

        LOG_INFO("IOAPIC: controller idx=%d id=%u base=0x%lx gsi=[%u,%u]", idx, controllers[idx].id,
                 node->ioapic.address, controllers[idx].gsi_start, controllers[idx].gsi_end);

        node = node->next;
    }

    if (num_controllers == 0) {
        LOG_WARN("IOAPIC: no controllers discovered; system may rely on legacy PIC only");
    }
}

// NOLINTNEXTLINE
void IOAPIC::route_legacy_irq(uint8_t irq, uint8_t vector, uint32_t dest_lapic_id) {
    uint32_t gsi = irq;
    size_t flags = IOAPIC_TRIGGER_EDGE | IOAPIC_POLARITY_HIGH;

    // Check for an ACPI Interrupt Source Override remapping this IRQ.
    IsoInfo* iso = find_iso(irq);
    if (iso) {
        gsi = iso->iso.gsi;

        uint16_t pol  = iso->iso.flags & ACPI_MADT_POLARITY_ACTIVE_LOW;
        uint16_t trig = (iso->iso.flags >> 2) & ACPI_MADT_TRIGGERING_LEVEL;

        if (pol == ACPI_MADT_POLARITY_ACTIVE_LOW) {
            flags |= IOAPIC_POLARITY_LOW;
        } else {
            flags |= IOAPIC_POLARITY_HIGH;
        }

        if (trig == ACPI_MADT_TRIGGERING_LEVEL) {
            flags |= IOAPIC_TRIGGER_LEVEL;
        } else {
            flags |= IOAPIC_TRIGGER_EDGE;
        }

        LOG_DEBUG("IOAPIC: ISO for IRQ %u -> GSI %u flags=0x%zx", irq, gsi, flags);
    }

    // NOLINTNEXTLINE
    route_gsi(gsi, vector, dest_lapic_id, flags | IOAPIC_DEST_PHYSICAL | IOAPIC_DELIVERY_FIXED);
}

// NOLINTNEXTLINE
void IOAPIC::route_gsi(uint32_t gsi, uint8_t vector, uint32_t dest_lapic_id, size_t flags) {
    int idx = get_controller_idx(gsi);

    if (idx == -1) {
        // If no IOAPIC owns this GSI, the mapping is either impossible
        // or we misparsed ACPI. Leave the configuration untouched.
        LOG_WARN("IOAPIC: route_gsi failed for GSI %u (no controller)", gsi);
        return;
    }

    uint32_t pin      = gsi - controllers[idx].gsi_start;
    uint32_t reg_low  = IOAPIC_REDTBL + (pin * 2);
    uint32_t reg_high = reg_low + 1;

    size_t entry = vector;
    entry |= flags;
    entry &= ~IOAPIC_MASKED;
    entry |= (static_cast<size_t>(dest_lapic_id) << 56);

    write(idx, reg_high, (entry >> 32) & 0xFFFFFFFF);
    write(idx, reg_low, entry & 0xFFFFFFFF);

    LOG_INFO("IOAPIC: route GSI %u -> vec=0x%x lapic=%u flags=0x%zx (ctrl=%d pin=%u)", gsi, vector,
             dest_lapic_id, flags, idx, pin);
}

void IOAPIC::mask_gsi(uint32_t gsi) {
    int idx = get_controller_idx(gsi);
    if (idx == -1) {
        LOG_WARN("IOAPIC: mask_gsi ignored for unknown GSI %u", gsi);
        return;
    }

    uint32_t pin     = gsi - controllers[idx].gsi_start;
    uint32_t reg_low = IOAPIC_REDTBL + (pin * 2);

    uint32_t val = read(idx, reg_low);
    val |= IOAPIC_MASKED;

    write(idx, reg_low, val);
    LOG_DEBUG("IOAPIC: masked GSI %u (ctrl=%d pin=%u)", gsi, idx, pin);
}

void IOAPIC::unmask_gsi(uint32_t gsi) {
    int idx = get_controller_idx(gsi);
    if (idx == -1) {
        LOG_WARN("IOAPIC: unmask_gsi ignored for unknown GSI %u", gsi);
        return;
    }

    uint32_t pin     = gsi - controllers[idx].gsi_start;
    uint32_t reg_low = IOAPIC_REDTBL + (pin * 2);

    uint32_t val = read(idx, reg_low);
    val &= ~IOAPIC_MASKED;

    write(idx, reg_low, val);
    LOG_DEBUG("IOAPIC: unmasked GSI %u (ctrl=%d pin=%u)", gsi, idx, pin);
}

void IOAPIC::mask_legacy_irq(uint8_t irq) {
    uint32_t gsi = irq;

    IsoInfo* iso = find_iso(irq);
    if (iso) {
        gsi = iso->iso.gsi;
    }

    mask_gsi(gsi);
}

void IOAPIC::unmask_legacy_irq(uint8_t irq) {
    uint32_t gsi = irq;

    IsoInfo* iso = find_iso(irq);
    if (iso) {
        gsi = iso->iso.gsi;
    }

    unmask_gsi(gsi);
}

void IOAPIC::send_eoi(uint8_t vector) {
    write(0, IOAPIC_EOI, vector);
}
}  // namespace kernel::hal