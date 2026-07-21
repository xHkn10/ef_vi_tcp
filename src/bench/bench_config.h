#pragma once

#include "types.h"

#include <random>
#include <array>

inline constexpr u64 N_BYTES = 100 * 1024 * 1024;
inline constexpr u32 MAX_SEND = 10'000;

// 00:0f:53:a3:ea:40
constexpr std::array<u8, 6> enp1s0f0_mac = {0x00, 0x0f, 0x53, 0xa3, 0xea, 0x40};
constexpr u32 enp1s0f0_ip = (192u << 24) | (168u << 16) | (100u << 8) | (2u << 0);
constexpr u16 enp1s0f0_port = 2069;

// 00:0f:53:a3:ea:41
constexpr std::array<u8, 6> enp1s0f1_mac = {0x00, 0x0f, 0x53, 0xa3, 0xea, 0x41};
// 192.168.100.1/24
constexpr u32 enp1s0f1_ip = (192u << 24) | (168u << 16) | (100u << 8) | (1u << 0);
constexpr u16 enp1s0f1_port = 2070;

inline std::mt19937 gen(42);
inline std::mt19937 shuffle_gen(1337);

auto pop_back(auto& container) {
    auto tmp = container.back();
    container.pop_back();
    return tmp;
}
