#include <concepts>

namespace kernel {
inline constexpr auto align_down(std::unsigned_integral auto n, std::unsigned_integral auto base) {
    return uintptr_t(n) & ~(uintptr_t(base) - 1);
}

inline constexpr auto align_up(std::unsigned_integral auto n, std::unsigned_integral auto base) {
    return align_down(n + base - 1, base);
}

inline constexpr auto div_roundup(std::unsigned_integral auto n, std::unsigned_integral auto base) {
    return align_up(n, base) / base;
}

inline constexpr bool is_aligned(std::unsigned_integral auto n, std::unsigned_integral auto base) {
    return (uintptr_t(n) & (uintptr_t(base) - 1)) == 0;
}
}  // namespace kernel