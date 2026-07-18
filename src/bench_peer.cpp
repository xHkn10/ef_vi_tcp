#include "tcp/socket.h"

#include <net/if.h>

namespace {
    namespace {
        // 00:0f:53:a3:ea:40
        constexpr std::array<u8, 6> enp1s0f0_mac = {0x00, 0x0f, 0x53, 0xa3, 0xea, 0x40};
        constexpr u32 enp1s0f0_ip = (192u << 24) | (168u << 16) | (100u << 8) | (2u << 0);
        constexpr u16 remote_port = 2069;
    }

    namespace {
        //00:0f:53:a3:ea:41
        constexpr std::array<u8, 6> enp1s0f1_mac = {0x00, 0x0f, 0x53, 0xa3, 0xea, 0x41};
        // 192.168.100.1/24
        constexpr u32 enp1s0f1_ip = (192u << 24) | (168u << 16) | (100u << 8) | (1u << 0);
        constexpr u16 local_port = 2070;
    }
}

int main() {
    static_assert(ENABLE_PASSIVE_OPEN, "build bench_peer with -DEFVI_TCP_BENCH_PEER");

    if (if_nametoindex(io::INTERFACE_NAME) == 0) {
        LOG_ERROR("Failed to find interface %s", io::INTERFACE_NAME);
        return 1;
    }

    tcp::socket sock;
    sock.bind(enp1s0f1_ip, local_port, enp1s0f0_ip, remote_port, enp1s0f0_mac, enp1s0f1_mac);
    sock.listen();

    for (;;)
        sock.poll();
}
