#pragma once

#include "uacpi/acpi.h"

namespace kernel::hal {
struct LapicInfo {
    LapicInfo* next;
    acpi_madt_lapic lapic;

    static LapicInfo* head();
};

struct IoApicInfo {
    IoApicInfo* next;
    acpi_madt_ioapic ioapic;

    static IoApicInfo* head();
};

struct IsoInfo {
    IsoInfo* next;
    acpi_madt_interrupt_source_override iso;

    static IsoInfo* head();
};

struct X2ApicInfo {
    X2ApicInfo* next;
    acpi_madt_x2apic x2apic;

    static X2ApicInfo* head();
};
}  // namespace kernel::hal