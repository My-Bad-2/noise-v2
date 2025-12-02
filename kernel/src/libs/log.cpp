/**
 * @file log.cpp
 * @brief Implementation of the kernel logging backend.
 *
 * This file provides the concrete implementation of
 * `kernel::__details::Logger`. It prints colorized log messages using
 * stdio-style functions and halts the system on panic.
 */

#include <stdio.h>
#include "arch.hpp"
#include "libs/log.hpp"

namespace kernel {
namespace __details {
namespace {

// ANSI color escape sequences for different log levels.
constexpr const char* COLOR_RESET   = "\033[0m";
constexpr const char* COLOR_GREY    = "\033[90m";
constexpr const char* COLOR_WHITE   = "\033[37m";
constexpr const char* COLOR_YELLOW  = "\033[33m";
constexpr const char* COLOR_RED     = "\033[31m";
constexpr const char* COLOR_MAGENTA = "\033[35m";

}  // namespace

const char* Logger::level_to_string(LogLevel level) {
    // Map log level to a short tag.
    switch (level) {
        case LogLevel::Debug:
            return "DBG";
        case LogLevel::Info:
            return "INF";
        case LogLevel::Warning:
            return "WRN";
        case LogLevel::Error:
            return "ERR";
        case LogLevel::Fatal:
            return "FTL";
        default:
            return "?";
    }
}

const char* Logger::level_to_color(LogLevel level) {
    // Map log level to a color escape sequence.
    switch (level) {
        case LogLevel::Debug:
            return COLOR_GREY;
        case LogLevel::Info:
            return COLOR_WHITE;
        case LogLevel::Warning:
            return COLOR_YELLOW;
        case LogLevel::Error:
            return COLOR_RED;
        case LogLevel::Fatal:
            return COLOR_MAGENTA;
        default:
            return COLOR_RESET;
    }
}

void Logger::log(LogLevel level, const char* file, int line, const char* format, ...) {
    const char* color     = level_to_color(level);
    const char* level_str = level_to_string(level);

    // Prefix: [LEVEL] (file:line)
    printf("%s[%s] (%s:%d) ", color, level_str, file, line);

    // Forward the variable arguments to vprintf.
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    // Reset color and terminate with newline.
    printf("%s\n", COLOR_RESET);
}

void Logger::panic(const char* file, int line, const char* format, ...) {
    // Print a panic prefix and message in magenta.
    printf("%s[PANIC] (%s:%d)", COLOR_MAGENTA, file, line);

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    printf("%s\n", COLOR_RESET);

    // Halt the system with interrupts disabled.
    arch::halt(false);
}

}  // namespace __details
}  // namespace kernel