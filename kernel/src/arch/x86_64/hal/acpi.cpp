#include "hal/acpi.hpp"
#include <string.h>
#include "libs/log.hpp"
#include "memory/heap.hpp"
#include "uacpi/acpi.h"
#include "uacpi/status.h"
#include "uacpi/tables.h"
#include "hal/apic.hpp"

namespace kernel::hal {
namespace {
LapicInfo* lapic_list   = nullptr;
IoApicInfo* ioapic_list = nullptr;
IsoInfo* iso_list       = nullptr;
X2ApicInfo* x2apic_list = nullptr;
acpi_madt* hdr          = nullptr;

void add_lapic(acpi_madt_lapic& lapic) {
    LapicInfo* node = new LapicInfo;
    node->next      = lapic_list;
    node->lapic     = lapic;
    lapic_list      = node;
}

void add_ioapic(acpi_madt_ioapic& ioapic) {
    IoApicInfo* node = new IoApicInfo;
    node->next       = ioapic_list;
    node->ioapic     = ioapic;
    ioapic_list      = node;
}

void add_iso(acpi_madt_interrupt_source_override& iso) {
    IsoInfo* node = new IsoInfo;
    node->next    = iso_list;
    node->iso     = iso;
    iso_list      = node;
}

void add_x2apic(acpi_madt_x2apic& x2apic) {
    X2ApicInfo* node = new X2ApicInfo;
    node->next       = x2apic_list;
    node->x2apic     = x2apic;
    x2apic_list      = node;
}
}  // namespace

void ACPI::parse_tables() {
    uacpi_table out_table;

    // MADT is the primary ACPI table for interrupt/controller topology
    // (LAPICs, IOAPICs, interrupt source overrides, x2APICs, etc.).
    if (uacpi_table_find_by_signature(ACPI_MADT_SIGNATURE, &out_table) != UACPI_STATUS_OK) {
        LOG_WARN("ACPI: MADT not found; APIC-based interrupt setup will be limited");
        return;
    }

    acpi_madt* ptr = static_cast<acpi_madt*>(out_table.ptr);

    // Copy MADT into kernel-owned memory so we can safely unref the uACPI
    // backing storage and still walk entries later.
    hdr = reinterpret_cast<acpi_madt*>(memory::kmalloc(ptr->hdr.length));
    if (!hdr) {
        LOG_ERROR("ACPI: failed to allocate MADT copy (len=%u)", ptr->hdr.length);
        uacpi_table_unref(&out_table);
        return;
    }

    memcpy(hdr, ptr, ptr->hdr.length);
    uacpi_table_unref(&out_table);

    LOG_INFO("ACPI: MADT copied to %p (length=%u)", static_cast<void*>(hdr), hdr->hdr.length);

    const uintptr_t start = reinterpret_cast<uintptr_t>(hdr->entries);
    const uintptr_t end   = reinterpret_cast<uintptr_t>(hdr) + hdr->hdr.length;

    acpi_entry_hdr* madt = reinterpret_cast<acpi_entry_hdr*>(start);
    uintptr_t entry      = start;

    while (entry < end) {
        switch (madt->type) {
            case ACPI_MADT_ENTRY_TYPE_LAPIC: {
                // Local APIC per-CPU descriptors (APIC ID, enabled state).
                auto* lapic = reinterpret_cast<acpi_madt_lapic*>(entry);
                add_lapic(*lapic);
                LOG_DEBUG("ACPI: LAPIC entry apic_id=%u flags=0x%x", lapic->id, lapic->flags);
                break;
            }
            case ACPI_MADT_ENTRY_TYPE_IOAPIC: {
                // IOAPIC controllers for external interrupts.
                auto* ioapic = reinterpret_cast<acpi_madt_ioapic*>(entry);
                add_ioapic(*ioapic);
                LOG_DEBUG("ACPI: IOAPIC entry id=%u addr=0x%x gsi_base=%u", ioapic->id,
                          ioapic->address, ioapic->gsi_base);
                break;
            }
            case ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE: {
                // Overrides for legacy PIC IRQs (e.g. remapped timer/keyboard).
                auto* iso = reinterpret_cast<acpi_madt_interrupt_source_override*>(entry);
                add_iso(*iso);
                LOG_DEBUG("ACPI: ISO entry bus=%u src_irq=%u gsi=%u flags=0x%x", iso->bus,
                          iso->source, iso->gsi, iso->flags);
                break;
            }
            case ACPI_MADT_ENTRY_TYPE_LOCAL_X2APIC: {
                // x2APIC LAPIC entries for systems using logical APIC IDs.
                auto* x2 = reinterpret_cast<acpi_madt_x2apic*>(entry);
                add_x2apic(*x2);
                LOG_DEBUG("ACPI: x2APIC entry id=%u flags=0x%x", x2->id, x2->flags);
                break;
            }
            default:
                // For now, silently skip unknown MADT entry types.
                break;
        }

        entry += madt->length;

        madt = reinterpret_cast<acpi_entry_hdr*>(entry);
    }

    LOG_INFO("ACPI: MADT parse complete (lapic/ioapic/iso/x2apic lists built)");
}

LapicInfo* LapicInfo::head() {
    return lapic_list;
}

IoApicInfo* IoApicInfo::head() {
    return ioapic_list;
}

IsoInfo* IsoInfo::head() {
    return iso_list;
}

X2ApicInfo* X2ApicInfo::head() {
    return x2apic_list;
}
}  // namespace kernel::hal
