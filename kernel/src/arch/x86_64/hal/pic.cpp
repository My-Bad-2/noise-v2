#include "hal/pic.hpp"
#include "hal/io.hpp"
#include "libs/log.hpp"

#define CASCADE_IRQ  2
#define PIC1_COMMAND 0x20
#define PIC1_DATA    (PIC1_COMMAND + 1)
#define PIC2_COMMAND 0XA0
#define PIC2_DATA    (PIC2_COMMAND + 1)

#define ICW1_ICW4      0x01
#define ICW1_SINGLE    0x02
#define ICW1_INTERVAL4 0x04
#define ICW1_LEVEL     0x008
#define ICW1_INIT      0x10

#define ICW4_8086       0x01
#define ICW4_AUTO       0x02
#define ICW4_BUF_SLAVE  0x08
#define ICW4_BUF_MASTER 0x0C
#define ICW4_SFNM       0x10

#define PIC_EOI      0x20
#define PIC_READ_IRR 0x0A
#define PIC_READ_ISR 0x0B

namespace kernel::hal {
uint16_t LegacyPIC::get_irq_reg(uint8_t ocw3) {
    out<uint8_t>(PIC1_COMMAND, ocw3);
    out<uint8_t>(PIC2_COMMAND, ocw3);

    return in<uint8_t>(PIC1_COMMAND) | (in<uint8_t>(PIC2_COMMAND) << 8);
}

void LegacyPIC::remap() {
    out<uint8_t>(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    out<uint8_t>(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    // ICW2: Master PIC vector offset
    out<uint8_t>(PIC1_DATA, 0x20);
    io_wait();

    // ICW2: Slave PIC vector offset
    out<uint8_t>(PIC2_DATA, 0x28);
    io_wait();

    // ICW3: tell Master PIC that there is a slave PIC at IRQ2
    out<uint8_t>(PIC1_DATA, 1 << CASCADE_IRQ);
    io_wait();

    // ICW3: tell Slave PIC its cascade identity (0000 0010)
    out<uint8_t>(PIC2_DATA, CASCADE_IRQ);
    io_wait();

    // ICW4: have the PICs use 8086 mode (and not 8080 mode)
    out<uint8_t>(PIC1_DATA, ICW4_8086);
    io_wait();
    out<uint8_t>(PIC2_DATA, ICW4_8086);
    io_wait();

    LOG_DEBUG("Remapped PIC: master_offset: 0x20 slave_offset: 0x28");

    // Mask all IRQs
    disable();
}

void LegacyPIC::disable() {
    LOG_DEBUG("Masking all Legacy IRQs!");

    out<uint8_t>(PIC1_DATA, 0xFF);
    out<uint8_t>(PIC2_DATA, 0xFF);
}

void LegacyPIC::set_mask(uint8_t irq) {
    LOG_DEBUG("Masking Legacy Vector 0x%x", irq);

    uint8_t port = PIC1_DATA;
    irq -= 0x20;

    if (irq >= 8) {
        port = PIC2_DATA;
        irq -= 8;
    }

    uint8_t val = in<uint8_t>(port) | (1 << irq);
    out<uint8_t>(port, val);
}

void LegacyPIC::clear_mask(uint8_t irq) {
    LOG_DEBUG("Unmasking Legacy Vector 0x%x", irq);

    uint8_t port = PIC1_DATA;
    irq -= 0x20;

    if (irq >= 8) {
        port = PIC2_DATA;
        irq -= 8;
    }

    uint8_t val = in<uint8_t>(port) & ~(1 << irq);
    out<uint8_t>(port, val);
}

void LegacyPIC::eoi(uint8_t irq) {
    irq -= 0x20;

    if (irq >= 8) {
        out<uint8_t>(PIC2_COMMAND, PIC_EOI);
    }

    out<uint8_t>(PIC1_COMMAND, PIC_EOI);
}

uint16_t LegacyPIC::get_irr() {
    return get_irq_reg(PIC_READ_IRR);
}

uint16_t LegacyPIC::get_isr() {
    return get_irq_reg(PIC_READ_ISR);
}
}  // namespace kernel::hal