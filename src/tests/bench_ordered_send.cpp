#include "tcp/socket.h"
#include <random>
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

std::mt19937 gen(42);
constexpr int N_BYTES = 100 * 1024 * 1024;
constexpr auto MAX_SEND = 10'000;

static_assert(!ENABLE_PASSIVE_OPEN);

int main() {
    if (if_nametoindex(io::INTERFACE_NAME) == 0) {
        LOG_ERROR("Failed to find interface %s", io::INTERFACE_NAME);
        return 1;
    }

    io::cycle_timer::elapse(5000);

    tcp::socket sock;
    sock.bind(enp1s0f0_ip, local_port, enp1s0f1_ip, remote_port, enp1s0f1_mac, enp1s0f0_mac);
    sock.connect();

    while (!sock.is_established())
        sock.poll();

    std::puts("Ordered send peer connected\n");

    const std::vector<std::byte> bytes = [] {
        auto ret = std::vector<std::byte>(MAX_SEND);
        for (auto& b : ret)
            b = static_cast<std::byte>(rand());
        return ret;
    }();

    const auto t0 = io::cycle_timer::now();
    {
        auto left = N_BYTES;
        while (left) {
            const auto amount = std::uniform_int_distribution{1, std::min(left, MAX_SEND)}(gen);
            std::span bytes_span = std::span{bytes}.first(amount);
            while (!bytes_span.empty()) {
                const auto actual_sent = sock.send(bytes_span);
                left -= actual_sent;
                bytes_span = bytes_span.subspan(actual_sent);
                sock.poll();
            }
        }
        while (!sock.test_tcb().tx_unacked.empty())
            sock.poll();
    }
    const auto t1 = io::cycle_timer::now();

    const double bench_ms = static_cast<double>(t1 - t0) / io::cycle_timer::cycles_per_ms;

    std::printf("Ordered byte send took %f ms\n", bench_ms);
}
