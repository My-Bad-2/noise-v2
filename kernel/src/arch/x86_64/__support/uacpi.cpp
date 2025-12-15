#include "cpu/exception.hpp"
#include "uacpi/kernel_api.h"
#include "hal/io.hpp"
#include "uacpi/status.h"
#include "hal/interface/interrupt.hpp"
#include "hal/interrupt.hpp"
#include "uacpi/types.h"

using namespace kernel::hal;
using namespace kernel::cpu;

class UacpiIrqAdapter : public IInterruptHandler {
   private:
    uacpi_interrupt_handler handler;
    uacpi_handle context;

   public:
    UacpiIrqAdapter(uacpi_interrupt_handler handler, uacpi_handle ctx)
        : handler(handler), context(ctx) {}

    virtual ~UacpiIrqAdapter() = default;

    IrqStatus handle(arch::TrapFrame* frame) override {
        uacpi_interrupt_ret ret = this->handler(this->context);

        if (ret == UACPI_INTERRUPT_HANDLED) {
            return IrqStatus::Handled;
        }

        return IrqStatus::Unhandled;
    }

    const char* name() const override {
        return "uACPI SCI Handler";
    }

    bool matches(uacpi_interrupt_handler h) const {
        return this->handler == h;
    }
};

struct AdapterNode {
    uacpi_u32 irq;
    UacpiIrqAdapter* adapter;
    AdapterNode* next;
};

namespace {
AdapterNode* adapter_list = nullptr;
}

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

// NOLINTNEXTLINE
uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size len, uacpi_handle* out_handle) {
    // NOLINTNEXTLINE
    *out_handle = reinterpret_cast<uacpi_handle>(base);
    return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle) {}

uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq, uacpi_interrupt_handler handler,
                                                    uacpi_handle ctx,
                                                    uacpi_handle* out_irq_handle) {
    UacpiIrqAdapter* adapter = new UacpiIrqAdapter(handler, ctx);

    if (!adapter) {
        return UACPI_STATUS_OUT_OF_MEMORY;
    }

    arch::InterruptDispatcher::register_handler(static_cast<uint8_t>(irq), adapter);

    AdapterNode* node = new AdapterNode();
    node->irq         = irq;
    node->adapter     = adapter;
    node->next        = adapter_list;
    adapter_list      = node;

    if (out_irq_handle) {
        *out_irq_handle = static_cast<uacpi_handle>(adapter);
    }

    return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler handler,
                                                      uacpi_handle irq_handle) {
    UacpiIrqAdapter* target_adapter = reinterpret_cast<UacpiIrqAdapter*>(irq_handle);
    AdapterNode** curr              = &adapter_list;

    while (*curr) {
        AdapterNode* node = *curr;

        if (node->adapter == target_adapter) {
            if (!node->adapter->matches(handler)) {
                return UACPI_STATUS_INVALID_ARGUMENT;
            }

            arch::InterruptDispatcher::unregister_handler(static_cast<uint8_t>(node->irq));

            delete node->adapter;

            *curr = node->next;
            delete node;

            return UACPI_STATUS_OK;
        }

        curr = &(*curr)->next;
    }

    return UACPI_STATUS_NOT_FOUND;
}