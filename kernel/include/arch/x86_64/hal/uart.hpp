#pragma once

#include "hal/interface/uart.hpp"

#define COM1_PORT 0x3F8
#define COM2_PORT 0x2E8
#define COM3_PORT 0x3E8
#define COM4_PORT 0x2E8

namespace kernel::hal {

class UART16550 : public IUART {
   public:
    explicit UART16550(uint16_t port = COM1_PORT) : port_base(port) {}

    bool init(uint32_t baud_rate) override;
    void send_char(char c) override;

    char recieve_char() override;
    bool is_data_ready() override;
    bool is_tx_ready() override;

   private:
    void write(uint16_t reg, uint8_t value) const;
    uint8_t read(uint16_t reg) const;

    uint16_t port_base;
};

}  // namespace kernel::hal