#include "tcp/socket.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <iostream>
#include <memory>

#include "engine/Engine.h"

constexpr int N = 1;

namespace {
    constexpr std::string_view username = "hakan";
    constexpr std::string_view pass = "PASS";

    namespace {
        // 00:0f:53:a3:ea:40
        constexpr std::array<u8, 6> enp1s0f0_mac = {0x00, 0x0f, 0x53, 0xa3, 0xea, 0x40};
        constexpr u32 enp1s0f0_ip = (192u << 24) | (168u << 16) | (100u << 8) | (2u << 0);
        constexpr u16 local_port = 2069;
    }

    namespace {
        //00:0f:53:a3:ea:41
        constexpr std::array<u8, 6> enp1s0f1_mac = {0x00, 0x0f, 0x53, 0xa3, 0xea, 0x41};
        // 192.168.100.1/24
        constexpr u32 enp1s0f1_ip = (192u << 24) | (168u << 16) | (100u << 8) | (1u << 0);
        constexpr u16 remote_port = 2070;
    }
}

static_assert(!ENABLE_PASSIVE_OPEN);

int main() {
    if (if_nametoindex(io::INTERFACE_NAME) == 0) {
        LOG_ERROR("Failed to find interface %s", io::INTERFACE_NAME);
        return 1;
    }

    auto engine = std::make_unique<engine::Engine<N>>();

    auto& cfg = engine->accounts[0].cfg;

    cfg = {
        .smac = enp1s0f0_mac,
        .dmac = enp1s0f1_mac,
        .local_ip = enp1s0f0_ip,
        .remote_ip = enp1s0f1_ip,
        .local_port = local_port,
        .remote_port = remote_port,
        .username = username,
        .pass = pass
    };

    bool res = engine->accounts[0].sock.bind(cfg.local_ip, cfg.local_port, cfg.remote_ip, cfg.remote_port, cfg.dmac, cfg.smac);
    if (!res)
        return 1;

    engine->run();
}
