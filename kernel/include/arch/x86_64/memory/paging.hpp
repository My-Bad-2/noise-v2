/**
 * @file paging.hpp
 * @brief Core paging flag constants and TLB management helpers.
 *
 * The `Flag*` constants represent architectural PTE bits used by the
 * paging implementation. `TLB` wraps common invalidation patterns
 * (single-page, context-wide, etc.) behind a simple interface.
 */

#pragma once

#include <cstdint>

namespace kernel::memory {
constexpr uint64_t FlagPresent      = 1ULL << 0;
constexpr uint64_t FlagWrite        = 1ULL << 1;
constexpr uint64_t FlagUser         = 1ULL << 2;
constexpr uint64_t FlagWriteThrough = 1ULL << 3;
constexpr uint64_t FlagCacheDisable = 1ULL << 4;
constexpr uint64_t FlagAccessed     = 1ULL << 5;
constexpr uint64_t FlagDirty        = 1ULL << 6;
constexpr uint64_t FlagHuge         = 1ULL << 7;
constexpr uint64_t FlagGlobal       = 1ULL << 8;
constexpr uint64_t FlagNoExec       = 1ULL << 63;

constexpr uint64_t FlagPAT  = 1ULL << 7;
constexpr uint64_t FlagLPAT = 1ULL << 12;

constexpr uint64_t page_mask = 0x000FFFFFFFFFF000ULL;

/**
 * @brief TLB invalidation utility.
 *
 * Abstracts over different invalidation strategies:
 *  - `flush` for a single page.
 *  - `flush_specific` / `flush_context` for PCID-tagged spaces.
 *  - `flush_all` / `flush_hard` as coarse fallbacks.
 *
 * The implementation adapts based on whether INVPCID/PCID are supported.
 */
class TLB {
   public:
    static bool has_invpcid;

    static void flush(uintptr_t virt_addr);
    static void flush_specific(uintptr_t virt_addr, uint16_t pcid);
    static void flush_context(uint16_t pcid);
    static void flush_all();
    static void flush_hard();
};
}  // namespace kernel::memory