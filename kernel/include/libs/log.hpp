/**
 * @file log.hpp
 * @brief Simple logging interface and convenience macros.
 *
 * This header defines log-level macros (`LOG_DEBUG`, `LOG_INFO`, etc.)
 * that capture the source file and line number, and forward formatted
 * messages to the `kernel::__details::Logger` backend.
 *
 * The implementation assumes that a basic console output mechanism
 * is available.
 */
#pragma once

#include <cstdint>

// Convenience macros: capture file and line and forward to Logger.
// Usage:
//   LOG_INFO("Init complete, rc=%d", rc);
//   LOG_ERROR("Failed to do thing: %s", reason);

#define LOG_DEBUG(fmt, ...)                                                                     \
    kernel::__details::Logger::log(kernel::__details::LogLevel::Debug, __FILE_NAME__, __LINE__, fmt, \
                                   ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)                                                                      \
    kernel::__details::Logger::log(kernel::__details::LogLevel::Info, __FILE_NAME__, __LINE__, fmt, \
                                   ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)                                                                          \
    kernel::__details::Logger::log(kernel::__details::LogLevel::Warning, __FILE_NAME__, __LINE__, fmt, \
                                   ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)                                                                     \
    kernel::__details::Logger::log(kernel::__details::LogLevel::Error, __FILE_NAME__, __LINE__, fmt, \
                                   ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...)                                                                     \
    kernel::__details::Logger::log(kernel::__details::LogLevel::Fatal, __FILE_NAME__, __LINE__, fmt, \
                                   ##__VA_ARGS__)

// PANIC helper: prints a panic message and halts the CPU via Logger::panic().
#define PANIC(fmt, ...) kernel::__details::Logger::panic(__FILE_NAME__, __LINE__, fmt, ##__VA_ARGS__)

namespace kernel {
namespace __details {

/**
 * @brief Log severity levels understood by the logger.
 *
 * The numeric ordering reflects increasing severity.
 */
enum class LogLevel : uint8_t { Debug = 0, Info, Warning, Error, Fatal };

/**
 * @brief Simple, static logging backend.
 *
 * `Logger` provides two main entry points:
 *  - `log()`   : print a formatted message with level, file, and line.
 *  - `panic()` : print a panic message and stop execution (via `arch::halt`).
 *
 * The actual output mechanism is provided by the implementation.
 */
class Logger {
   public:
    /**
     * @brief Log a formatted message with a given severity.
     *
     * Typical usage is via the convenience macros defined above, which
     * fill in @p file and @p line automatically.
     *
     * @param level  Log severity.
     * @param file   Short file name of the caller.
     * @param line   Source line number in the caller.
     * @param format printf-style format string.
     * @param ...    Additional arguments for the format string.
     */
    static void log(LogLevel level, const char* file, int line, const char* format, ...);

    /**
     * @brief Log a panic message and abort execution.
     *
     * After printing the panic message, this function never returns and
     * will typically halt the CPU in an infinite loop.
     *
     * @param file   Short file name of the caller.
     * @param line   Source line number in the caller.
     * @param format printf-style format string.
     * @param ...    Additional arguments for the format string.
     */
    [[noreturn]] static void panic(const char* file, int line, const char* format, ...);

   private:
    /**
     * @brief Convert a log level to a short string tag.
     *
     * Example: Debug -> "DBG", Error -> "ERR".
     */
    static const char* level_to_string(LogLevel level);

    /**
     * @brief Map a log level to an ANSI color escape code.
     *
     * Used to colorize log output when a terminal that supports ANSI
     * escape sequences is attached (e.g. a serial console).
     */
    static const char* level_to_color(LogLevel level);
};

}  // namespace __details
}  // namespace kernel