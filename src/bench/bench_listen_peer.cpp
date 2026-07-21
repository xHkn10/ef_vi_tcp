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
    sock.bind(enp1s0f1_ip, enp1s0f0_port, enp1s0f0_ip, enp1s0f1_port, enp1s0f0_mac, enp1s0f1_mac);
    sock.listen();

    while (true) {
        while (!sock.is_established())
            sock.poll();

        while (sock.is_established()) {
            sock.poll();
            if (auto sgl = sock.receive_available(); sgl.head)
                sock.consume(sgl, sgl.n_bytes);
        }
    }
}
