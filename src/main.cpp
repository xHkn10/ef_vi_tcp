#include "SfTcpSocket.h"
#include <arpa/inet.h>
#include <net/if.h>
#include <iostream>
#include <vector>

int main() {
    int ifindex = if_nametoindex("enp1s0f0");
    if (ifindex == 0) {
        std::cerr << "Failed to find interface enp1s0f0\n";
        return 1;
    }

    SfTcpSocket sock{};

    u32 local_ip = inet_addr("10.10.10.1");
    u16 local_port = 50000;
    u32 remote_ip = inet_addr("172.16.10.35");
    u16 remote_port = 12345;
    // 10:7b:44:92:90:ca
    u8 smac[6] = {0x00, 0x0f, 0x53, 0xa3, 0xea, 0x40};
    u8 dmac[6] = {0x10, 0x7b, 0x44, 0x92, 0x90, 0xca};

    sock.bind(ntohl(local_ip), local_port);

    if (!sock.connect(ntohl(remote_ip), remote_port, dmac, smac)) {
        std::cerr << "Connection failed.\n";
        return 1;
    }

    std::string packet = "sa dunya\n";
    sock.send({reinterpret_cast<const std::byte*>(packet.data()), packet.size()});
}
