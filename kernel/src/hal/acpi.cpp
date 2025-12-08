#include "hal/acpi.hpp"
#include "uacpi/context.h"
#include "uacpi/uacpi.h"
#include "libs/log.hpp"

#define EARLY_TBL_BUFFER_SIZE 1024

namespace kernel::hal {
uint8_t* ACPI::early_tbl_buff = nullptr;
acpi_fadt* ACPI::fadt         = nullptr;

void ACPI::bootstrap() {
    // The early table buffer is a scratch area that uACPI uses before the
    // full memory subsystem is up. We allocate it once and reuse it.
    if (!early_tbl_buff) {
        early_tbl_buff = new uint8_t[EARLY_TBL_BUFFER_SIZE];
        LOG_INFO("ACPI: allocated early table buffer (%u bytes)", EARLY_TBL_BUFFER_SIZE);
    }

    // Use an informational log level from uACPI so we see warnings/errors
    // without being overwhelmed by debug noise.
    uacpi_context_set_log_level(UACPI_LOG_INFO);
    uacpi_setup_early_table_access(early_tbl_buff, EARLY_TBL_BUFFER_SIZE);

    // Discover the FADT; this gives us global ACPI configuration like
    // power management, reboot, and the location of other tables.
    if (uacpi_table_fadt(&fadt) != UACPI_STATUS_OK) {
        LOG_WARN("ACPI: FADT not found; some power/PM features may be unavailable");
        fadt = nullptr;
    } else {
        LOG_INFO("ACPI: FADT discovered at %p", static_cast<void*>(fadt));
    }

    // Populate internal structures from MADT and other tables.
    parse_tables();
}
}  // namespace kernel::hal