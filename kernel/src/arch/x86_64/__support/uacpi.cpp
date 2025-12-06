#include "uacpi/kernel_api.h"
#include "hal/io.hpp"

using namespace kernel::hal;

// NOLINTNEXTLINE
uacpi_status uacpi_kernel_io_write8(uacpi_handle handle, uacpi_size offset, uacpi_u8 in_value) {
    uintptr_t address = reinterpret_cast<uintptr_t>(handle) + offset;
    out<uint8_t>(static_cast<uint16_t>(address), in_value);
    return UACPI_STATUS_OK;
}

// NOLINTNEXTLINE
uacpi_status uacpi_kernel_io_write16(uacpi_handle handle, uacpi_size offset, uacpi_u16 in_value) {
    uintptr_t address = reinterpret_cast<uintptr_t>(handle) + offset;
    out<uint16_t>(static_cast<uint16_t>(address), in_value);
    return UACPI_STATUS_OK;
}

// NOLINTNEXTLINE
uacpi_status uacpi_kernel_io_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 in_value) {
    uintptr_t address = reinterpret_cast<uintptr_t>(handle) + offset;
    out<uint32_t>(static_cast<uint16_t>(address), in_value);
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read8(uacpi_handle handle, uacpi_size offset, uacpi_u8* out_value) {
    uintptr_t address = reinterpret_cast<uintptr_t>(handle) + offset;
    *out_value        = in<uint8_t>(static_cast<uint16_t>(address));
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16(uacpi_handle handle, uacpi_size offset, uacpi_u16* out_value) {
    uintptr_t address = reinterpret_cast<uintptr_t>(handle) + offset;
    *out_value        = in<uint16_t>(static_cast<uint16_t>(address));
    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32(uacpi_handle handle, uacpi_size offset, uacpi_u32* out_value) {
    uintptr_t address = reinterpret_cast<uintptr_t>(handle) + offset;
    *out_value        = in<uint32_t>(static_cast<uint16_t>(address));
    return UACPI_STATUS_OK;
}