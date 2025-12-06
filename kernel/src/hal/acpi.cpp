#include "hal/acpi.hpp"
#include "uacpi/context.h"
#include "uacpi/uacpi.h"

#define EARLY_TBL_BUFFER_SIZE 1024

namespace kernel::hal {
uint8_t* ACPI::early_tbl_buff = nullptr;
acpi_fadt* ACPI::fadt = nullptr;

void ACPI::init() {
    if(!early_tbl_buff) {
        early_tbl_buff = new uint8_t[EARLY_TBL_BUFFER_SIZE];
    }

    uacpi_context_set_log_level(UACPI_LOG_INFO);
    uacpi_setup_early_table_access(early_tbl_buff, EARLY_TBL_BUFFER_SIZE);

    uacpi_table_fadt(&fadt);
}
}