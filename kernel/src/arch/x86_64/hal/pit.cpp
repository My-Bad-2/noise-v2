#include "hal/pit.hpp"
#include <cstdint>
#include "hal/io.hpp"
#include "internal/pit.h"
#include "libs/log.hpp"

namespace kernel::hal {
void PIT::init(uint32_t frequency) {
    uint8_t gate = in<uint8_t>(PORT_GATE_CTRL);
    out<uint8_t>(PORT_GATE_CTRL, gate & 0xFC);

    set_frequency(frequency);
}

void PIT::set_frequency(uint32_t frequency) {
    if (frequency == 0) {
        frequency = 18;
    }

    if (frequency > BASE_FREQUENCY_HZ) {
        frequency = BASE_FREQUENCY_HZ;
    }

    uint32_t divisor = BASE_FREQUENCY_HZ / frequency;

    if (divisor > 65535) {
        divisor = 0;
    }

    uint8_t cmd = CMD_MODE2 | CMD_ACCESS_LH;
    out<uint8_t>(PORT_COMMAND, cmd);
    io_wait();

    out<uint8_t>(PORT_CHANNEL0, static_cast<uint8_t>(divisor & 0xFF));
    io_wait();

    out<uint8_t>(PORT_CHANNEL0, static_cast<uint8_t>((divisor >> 8) & 0xFF));
}

void PIT::prepare_wait(uint16_t ms) {
    // if (ms > 50) {
    //     ms = 50;
    // }

    // uint32_t count = (BASE_FREQUENCY_HZ * ms) / 1000;
    uint8_t cmd    = CMD_CHANNEL2 | CMD_ACCESS_LH | CMD_MODE0;

    out<uint8_t>(PORT_COMMAND, cmd);

    out<uint8_t>(PORT_CHANNEL2, 0xFF);
    out<uint8_t>(PORT_CHANNEL2, 0xFF);

    uint8_t ctrl = in<uint8_t>(PORT_GATE_CTRL);
    out<uint8_t>(PORT_GATE_CTRL, (ctrl & 0xFD) | 1);
}

uint16_t PIT::read_count() {
    uint8_t cmd = CMD_CHANNEL2 | CMD_LATCH;
    out<uint8_t>(PORT_COMMAND, cmd);

    uint8_t lo = in<uint8_t>(PORT_CHANNEL2);
    uint8_t hi = in<uint8_t>(PORT_CHANNEL2);

    return (static_cast<uint16_t>(hi) << 8) | lo;
}

bool PIT::check_expiration() {
    uint8_t cmd = CMD_CHANNEL2 | CMD_READ_BACK | CMD_RW_HI;
    out<uint8_t>(PORT_COMMAND, cmd);

    uint8_t status = in<uint8_t>(PORT_CHANNEL2);
    return (status & 0x80);
}

void PIT::disable() {
    uint8_t ctrl = in<uint8_t>(PORT_GATE_CTRL);
    out<uint8_t>(PORT_GATE_CTRL, ctrl & 0xFE);
}

void PIT::wait_ticks(uint16_t ticks) {
    out<uint8_t>(PORT_COMMAND, CMD_MODE0 | CMD_BINARY | CMD_ACCESS_LH | CMD_SEL2);
    io_wait();

    out<uint8_t>(PORT_CHANNEL2, static_cast<uint8_t>(ticks & 0xFF));
    io_wait();

    out<uint8_t>(PORT_CHANNEL2, static_cast<uint8_t>((ticks >> 8) & 0xFF));
    io_wait();

    uint8_t ctrl = in<uint8_t>(PORT_GATE_CTRL);
    out<uint8_t>(PORT_GATE_CTRL, (ctrl & 0xFD) | 0x01);

    while (!(in<uint8_t>(PORT_GATE_CTRL) & 0x20)) {
        asm volatile("pause");
    }

    disable();
}

void PIT::udelay(uint32_t us) {
    const uint32_t max_chunk_us = 50000;

    while (us > 0) {
        uint32_t chunk_us = (us > max_chunk_us) ? max_chunk_us : us;
        uint32_t ticks    = (chunk_us * 1193182) / 1000000;

        if (ticks > 65535) {
            ticks = 65535;
        }

        wait_ticks(static_cast<uint16_t>(ticks));
        us -= chunk_us;
    }
}

void PIT::mdelay(uint32_t ms) {
    while (ms > 50) {
        wait_ticks(BASE_FREQUENCY_HZ * 50 / 1000);
        ms -= 50;
    }

    if (ms > 0) {
        wait_ticks(static_cast<uint16_t>(BASE_FREQUENCY_HZ * ms / 1000));
    }
}
}  // namespace kernel::hal