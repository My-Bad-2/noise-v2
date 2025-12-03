#include <concepts>

namespace kernel {
/**
 * @brief Align a value down to the nearest multiple of @p base.
 *
 * This is used pervasively in paging and PMM code to snap addresses
 * to page or large-page boundaries without branching.
 */
inline constexpr auto align_down(std::unsigned_integral auto n, std::unsigned_integral auto base) {
    return uintptr_t(n) & ~(uintptr_t(base) - 1);
}

/**
 * @brief Align a value up to the nearest multiple of @p base.
 *
 * Implemented in terms of @ref align_down to keep the rounding logic
 * simple and consistent: add (base-1), then truncate.
 */
inline constexpr auto align_up(std::unsigned_integral auto n, std::unsigned_integral auto base) {
    return align_down(n + base - 1, base);
}

/**
 * @brief Divide, rounding up to the next integer.
 *
 * This lets the code work in *units* (pages, entries) while still
 * reasoning in bytes, without manual off-by-one arithmetic.
 */
inline constexpr auto div_roundup(std::unsigned_integral auto n, std::unsigned_integral auto base) {
    return align_up(n, base) / base;
}

/**
 * @brief Check whether a value is aligned to @p base.
 *
 * This is used to decide whether we can use large pages or perform
 * alignment-sensitive mappings.
 */
inline constexpr bool is_aligned(std::unsigned_integral auto n, std::unsigned_integral auto base) {
    return (uintptr_t(n) & (uintptr_t(base) - 1)) == 0;
}
}  // namespace kernel