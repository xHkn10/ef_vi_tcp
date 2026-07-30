#pragma once

#include "types.h"

#include <array>
#include <random>
#include <string_view>


constexpr auto account_cnt = 1;

// 00:0f:53:a3:ea:41
constexpr std::array<u8, 6> us_mac = {0x00, 0x0f, 0x53, 0xa3, 0xea, 0x41};
constexpr u32 us_ip = (10u << 24) | (194u << 16) | (202u << 8) | (36u << 0);
constexpr u16 us_port = 31313;

// 00:0f:53:a3:ea:40
constexpr std::array<u8, 6> them_mac = {0x00, 0x0f, 0x53, 0xa3, 0xea, 0x40};
constexpr u32 them_ip = (10u << 24) | (194u << 16) | (202u << 8) | (35u << 0);
constexpr u16 them_port = 18039;

constexpr std::string_view username = "client";
constexpr std::string_view pass = "password";

// Scenario to run when none is given on argv.
constexpr std::string_view DEFAULT_SCENARIO = "split_length_prefix";

inline std::mt19937 gen(42);
