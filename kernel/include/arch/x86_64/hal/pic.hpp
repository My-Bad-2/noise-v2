#pragma once

#include <cstdint>

namespace kernel::hal {
class LegacyPIC {
   public:
    static void remap();
    static void disable();

    static void eoi(uint8_t irq);
    static void set_mask(uint8_t irq);
    static void clear_mask(uint8_t irq);

    static uint16_t get_irr();
    static uint16_t get_isr();

   private:
    static uint16_t get_irq_reg(uint8_t ocw3);
};
}  // namespace kernel::hal