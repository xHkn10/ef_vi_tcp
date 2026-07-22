#include "tcp/socket.h"
#include "bench_config.h"

#include <net/if.h>

static_assert(!ENABLE_PASSIVE_OPEN);

int main() {
    if (if_nametoindex(io::INTERFACE_NAME) == 0) {
        LOG_ERROR("Failed to find interface %s", io::INTERFACE_NAME);
        return 1;
    }

    tcp::socket sock;
    sock.bind(enp1s0f0_ip, enp1s0f0_port, enp1s0f1_ip, enp1s0f1_port, enp1s0f1_mac, enp1s0f0_mac);
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
            // const auto amount = std::uniform_int_distribution{1u, std::min<u32>(left, MAX_SEND)}(gen);
            const auto amount = std::min<u32>(left, MAX_SEND);
            std::span bytes_span = std::span{bytes}.first(amount);
            while (!bytes_span.empty()) {
                const auto actual_sent = sock.send(bytes_span);
                left -= actual_sent;
                bytes_span = bytes_span.subspan(actual_sent);
                sock.poll();
            }
        }
        while (!sock.tx_flushed())
            sock.poll();
    }
    const auto t1 = io::cycle_timer::now();

    const double bench_ms = static_cast<double>(t1 - t0) / io::cycle_timer::cycles_per_ms;

    std::printf("Ordered byte send took %f ms\n", bench_ms);

    constexpr auto n_gigabit = (double)N_BYTES / (1024 * 1024 * 1024) * 8;
    constexpr auto total_sent = n_gigabit * (78 + TCP_MAX_PAYLOAD_SZ) / TCP_MAX_PAYLOAD_SZ;

    const auto throughput = total_sent / (bench_ms / 1000);
    const auto goodput = n_gigabit / (bench_ms / 1000);

    std::printf("Throughput is %f Gbps\n", throughput);
    std::printf("Goodput is %f Gbps\n", goodput);
}
