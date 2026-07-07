#include "tcp/socket.h"
#include <arpa/inet.h>
#include <net/if.h>
#include <iostream>
#include <vector>

int main() {
    int ifindex = if_nametoindex("enp1s0f0");
    if (ifindex == 0) {
        LOG_ERROR("Failed to find interface enp1s0f0");
        return 1;
    }

    tcp::socket sock{};

    u32 local_ip = inet_addr("10.10.10.1");
    u16 local_port = 50000;
    u32 remote_ip = inet_addr("10.10.10.2");
    u16 remote_port = 12345;

    u8 smac[6] = {0x00, 0x0f, 0x53, 0xa3, 0xea, 0x40};
    u8 dmac[6] = {0x00, 0x0f, 0x53, 0xa3, 0xea, 0x41};  // enp1s0f1's MAC

    sock.bind(from_net(local_ip), local_port);

    if (!sock.connect(from_net(remote_ip), remote_port, dmac, smac))
        return 1;

    std::string packet = "sa dunya bla bla bla bla bla bla bla bla bla bla bla bla NUM: ";

    for (int cnt = 0; ; ++cnt) {
        if (cnt % (1 << 13) == 0) {
            LOG_INFO("packet %d sent", (cnt >> 13));
            auto packet_ = packet + std::to_string((cnt >> 13));
            sock.send({reinterpret_cast<const std::byte*>(packet_.data()), packet_.size()});
        }

        auto sgl = sock.receive_available();
        auto cur = sgl.head;

        if (sgl.n_bytes != 0) {
            LOG_INFO("RECEIVED:");
            while (cur) {
                for (auto b : cur->meta.payload)
                    std::putchar(static_cast<char>(b));
                std::puts("\n");
                cur = cur->meta.nxt;
            }
        }

        sock.consume(sgl, sgl.n_bytes);
    }
}
