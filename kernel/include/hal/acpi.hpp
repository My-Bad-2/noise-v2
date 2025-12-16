#pragma once

#include <cstdint>
#include "uacpi/tables.h"

namespace kernel::hal {
class ACPI {
   public:
    static void bootstrap();

   private:
    static void parse_tables();

    static acpi_fadt* fadt;
    static uint8_t* early_tbl_buff;
};
}  // namespace kernel::hal