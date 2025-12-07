#pragma once

#include <cstdint>
#include "uacpi/tables.h"
#include "hal/apic.hpp"

namespace kernel::hal {
/**
 * @brief High-level ACPI integration facade.
 *
 * This class coordinates ACPI bring-up via uACPI and exposes the minimal
 * information other HAL components care about (e.g. FADT, MADT-derived
 * LAPIC/IOAPIC topology).
 *
 * Why:
 *  - Centralizes ACPI initialization so the rest of the kernel doesn't
 *    need to talk to uACPI directly.
 *  - Provides a stable place to hang parsed ACPI-derived structures
 *    (interrupt routing, CPU topology) that APIC and timer code can use.
 */
class ACPI {
   public:
    /**
     * @brief Initialize ACPI support.
     *
     * Responsibilities:
     *  - Allocate an "early table buffer" used by uACPI while it locates
     *    tables before the full memory manager is ready.
     *  - Configure uACPI logging level.
     *  - Locate the FADT and MADT and hand them off to internal parsers.
     *
     * This is intended to be called once during early HAL initialization.
     */
    static void bootstrap();

   private:
    /// Parse ACPI tables of interest (currently MADT) into internal lists.
    static void parse_tables();

    /// Pointer to the FADT (Fixed ACPI Description Table) provided by uACPI.
    static acpi_fadt* fadt;
    /// Staging buffer backing uACPI's early table access API.
    static uint8_t* early_tbl_buff;
};
}  // namespace kernel::hal