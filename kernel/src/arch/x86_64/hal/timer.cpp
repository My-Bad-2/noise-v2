#include "hal/timer.hpp"
#include <cstdint>

namespace kernel::hal {
namespace {
/**
 * @brief Read the current timestamp counter (TSC).
 *
 * This provides a raw cycle counter used by timing and calibration
 * code. It is intentionally minimal and architecture-specific; higher
 * layers should interpret the result in terms of calibrated units.
 */
size_t rdtsc() {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<size_t>(hi) << 32) | lo;
}
}  // namespace
}  // namespace kernel::hal