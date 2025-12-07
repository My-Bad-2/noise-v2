#pragma once

#include "uacpi/acpi.h"

namespace kernel::hal {
/**
 * @brief Linked-list node describing one Local APIC (LAPIC) entry.
 *
 * Parsed from MADT LAPIC entries and used to discover which CPUs/APIC IDs
 * exist on the system for interrupt routing and CPU bring-up.
 */
struct LapicInfo {
    LapicInfo* next;
    acpi_madt_lapic lapic;

    static LapicInfo* head();
};

/**
 * @brief Linked-list node describing one IOAPIC.
 *
 * IOAPICs handle external interrupt routing; MADT IOAPIC entries are
 * stored here so the APIC code can program redirection tables later.
 */
struct IoApicInfo {
    IoApicInfo* next;
    acpi_madt_ioapic ioapic;

    static IoApicInfo* head();
};

/**
 * @brief Linked-list node describing an interrupt source override.
 *
 * Interrupt source overrides remap legacy IRQ lines (e.g. PIT, keyboard)
 * to different APIC input pins. The MADT ISO entries are captured here so
 * the PIC/APIC setup code can configure proper mappings.
 */
struct IsoInfo {
    IsoInfo* next;
    acpi_madt_interrupt_source_override iso;

    static IsoInfo* head();
};

/**
 * @brief Linked-list node describing a local x2APIC entry.
 *
 * On systems that expose x2APIC LAPICs via MADT, these records allow the
 * HAL to understand logical APIC IDs and associated CPUs.
 */
struct X2ApicInfo {
    X2ApicInfo* next;
    acpi_madt_x2apic x2apic;

    static X2ApicInfo* head();
};
}  // namespace kernel::hal