#pragma once

#include <cstdint>

#ifdef NOISE_DEBUG
#define LOG_DEBUG(fmt, ...)                                                                     \
    kernel::__details::Logger::log(kernel::__details::LogLevel::Debug, __FILE_NAME__, __LINE__, \
                                   fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...) static_cast<void>(0)
#endif

#define LOG_INFO(fmt, ...)                                                                     \
    kernel::__details::Logger::log(kernel::__details::LogLevel::Info, __FILE_NAME__, __LINE__, \
                                   fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)                                                                        \
    kernel::__details::Logger::log(kernel::__details::LogLevel::Warning, __FILE_NAME__, __LINE__, \
                                   fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)                                                                     \
    kernel::__details::Logger::log(kernel::__details::LogLevel::Error, __FILE_NAME__, __LINE__, \
                                   fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...)                                                                     \
    kernel::__details::Logger::log(kernel::__details::LogLevel::Fatal, __FILE_NAME__, __LINE__, \
                                   fmt, ##__VA_ARGS__)

#define PANIC(fmt, ...) \
    kernel::__details::Logger::panic(__FILE_NAME__, __LINE__, fmt, ##__VA_ARGS__)

namespace kernel {
namespace __details {
enum class LogLevel : uint8_t { Debug = 0, Info, Warning, Error, Fatal };

class Logger {
   public:
    static void log(LogLevel level, const char* file, int line, const char* format, ...);
    [[noreturn]] static void panic(const char* file, int line, const char* format, ...);

   private:
    static const char* level_to_string(LogLevel level);
    static const char* level_to_color(LogLevel level);
};

}  // namespace __details
}  // namespace kernel