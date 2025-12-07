#pragma once

#define BASE_FREQUENCY_HZ 1193182

// IO Ports
#define PORT_CHANNEL0  0x40
#define PORT_CHANNEL1  0x41  // Obsolete
#define PORT_CHANNEL2  0x42  // PC Speaker / Gate
#define PORT_COMMAND   0x43
#define PORT_GATE_CTRL 0x61  // PS/2 Controller B (Speaker)

// Command Register Bits
#define CMD_BINARY 0x00
#define CMD_BCD    0x01

#define CMD_MODE0 0x00  // Interrupt on Terminal Count
#define CMD_MODE1 0x02  // Hardware Retriggerable One-Shot
#define CMD_MODE2 0x04  // Rate Generator
#define CMD_MODE3 0x06  // Square Wave
#define CMD_MODE4 0x08  // Software Strobe
#define CMD_MODE5 0x0A  // Hardware Strobe

#define CMD_RW_LATCH 0x00
#define CMD_RW_LO    0x10
#define CMD_RW_HI    0x20
#define CMD_RW_BOTH  0x30

#define CMD_SEL0      0x00
#define CMD_SEL1      0x40
#define CMD_SEL2      0x80
#define CMD_READ_BACK 0xC0

#define CMD_CHANNEL0 0x00
#define CMD_CHANNEL1 0x40
#define CMD_CHANNEL2 0x80
#define CMD_CHANNEL3 0xc0

#define CMD_LATCH 0x00
#define CMD_ACCESS_LO 0x10
#define CMD_ACCESS_HI 0x20
#define CMD_ACCESS_LH 0x30