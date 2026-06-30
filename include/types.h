#pragma once

#include <cstdint>

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

inline bool seq_less(u32 s1, u32 s2) {
    return (s2 - s1) < (1u << 31);
}
