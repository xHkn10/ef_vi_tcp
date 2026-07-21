#include "tcp/socket.h"
#include "bench_config.h"

#include <net/if.h>

static_assert(ENABLE_PASSIVE_OPEN);

int main() {
    if (if_nametoindex(io::INTERFACE_NAME) == 0) {
        LOG_ERROR("Failed to find interface %s", io::INTERFACE_NAME);
        return 1;
    }

    tcp::socket sock;
    sock.bind(enp1s0f1_ip, enp1s0f1_port, enp1s0f0_ip, enp1s0f0_port, enp1s0f0_mac, enp1s0f1_mac);
    sock.listen();

    u64 rcvd_bytes = 0;

    while (true) {
        while (!sock.is_established())
            sock.poll();

        const auto t0 = io::cycle_timer::now();
        while (sock.is_established()) {
            sock.poll();
            if (auto sgl = sock.receive_available(); sgl.head) {
                rcvd_bytes += sgl.n_bytes;
                sock.consume(sgl, sgl.n_bytes);
            }
            if (rcvd_bytes >= N_BYTES)
                break;
        }
        const auto t1 = io::cycle_timer::now();

        const double bench_ms = static_cast<double>(t1 - t0) / io::cycle_timer::cycles_per_ms;

        if (rcvd_bytes == N_BYTES)
            std::printf("Received the bytes in %f ms\n", bench_ms);
        else if (rcvd_bytes > N_BYTES)
            std::puts("?");

        sock.close();
    }
}
