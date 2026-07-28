#include "tcp/socket.h"
#include "adversary_config.h"

#include <net/if.h>

static_assert(!ENABLE_PASSIVE_OPEN);

int main() {
    if (if_nametoindex(io::INTERFACE_NAME) == 0) {
        LOG_ERROR("Failed to find interface %s", io::INTERFACE_NAME);
        return 1;
    }

    tcp::socket sock;
    sock.bind(us_ip, us_port, them_ip, them_port, them_mac, us_mac);

    constexpr int N = 2000;
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
        io::cycle_timer::elapse(1);
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
