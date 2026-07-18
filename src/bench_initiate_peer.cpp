#include "tcp/socket.h"
#include <net/if.h>

namespace {
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

int main() {
    static_assert(!ENABLE_PASSIVE_OPEN);

    if (if_nametoindex(io::INTERFACE_NAME) == 0) {
        LOG_ERROR("Failed to find interface %s", io::INTERFACE_NAME);
        return 1;
    }

    tcp::socket sock;
    sock.bind(enp1s0f0_ip, local_port, enp1s0f1_ip, remote_port, enp1s0f1_mac, enp1s0f0_mac);

    constexpr int N = 10000;
    std::vector<u64> samples; samples.reserve(N);

    for (int i = 0; i < N; ++i) {
        const auto t0 = io::cycle_timer::now();
        sock.connect();
        do {
            sock.poll();
        } while (!sock.is_established());
        const auto t1 = io::cycle_timer::now();
        samples.push_back(static_cast<double>(t1 - t0) / io::cycle_timer::cycles_per_ms * 1000 * 1000);

        sock.abort();
        for (int j = 0; j < 20; ++j)
            sock.poll();
    }

    std::ranges::sort(samples);

    std::printf(
        "min: %luns, p50: %luns, p90: %luns, p95: %luns, p99: %luns, p999: %luns, max: %luns\n",
        samples.front(),
        samples[N / 2],
        samples[90 * N / 100],
        samples[95 * N / 100],
        samples[99 * N / 100],
        samples[999 * N / 1000],
        samples.back()
    );
}
