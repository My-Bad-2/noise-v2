#pragma once

#include <cstdint>
#include <cstddef>

namespace kernel::hal {
class IUART {
   public:
    virtual bool init(uint32_t baud_rate) = 0;

    virtual void send_char(char c) = 0;
    virtual char recieve_char()    = 0;

    virtual bool is_data_ready() = 0;
    virtual bool is_tx_ready()   = 0;

    void send_string(const char* str) {
        for (size_t i = 0; str[i] != '\0'; ++i) {
            this->send_char(str[i]);
        }
    }
};
}  // namespace kernel::hal