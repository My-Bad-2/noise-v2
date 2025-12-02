#include <concepts>

namespace kernel {
inline constexpr auto align_down(std::unsigned_integral auto addr,
                                 std::unsigned_integral auto base) {
    return uintptr_t(addr) & ~(uintptr_t(base) - 1);
}

inline constexpr auto align_up(std::unsigned_integral auto addr, std::unsigned_integral auto base) {
    return align_down(addr + base - 1, base);
}

inline constexpr auto div_roundup(std::unsigned_integral auto addr,
                                  std::unsigned_integral auto base) {
    return align_up(addr, base) / base;
}
}  // namespace kernel