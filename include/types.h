#pragma once

#include <bit>
#include <concepts>
#include <cstdint>

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i32 = std::int32_t;

inline bool seq_less(u32 s1, u32 s2) {
    return (s1 != s2) && ((s2 - s1) < (1u << 31));
}

template<std::integral T>
constexpr T to_net(T v) noexcept {
    if constexpr (std::endian::native == std::endian::little)
        return std::byteswap(v);
    else
        return v;
}

template<std::integral T>
constexpr T from_net(T v) noexcept {
    return to_net(v);
}
