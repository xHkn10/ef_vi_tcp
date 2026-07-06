#include "SfTcpSocket.h"
#include <arpa/inet.h>
#include <net/if.h>
#include <iostream>
#include <unistd.h>
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
    u32 remote_ip = inet_addr("10.10.10.2");
    u16 remote_port = 12345;

    u8 smac[6] = {0x00, 0x0f, 0x53, 0xa3, 0xea, 0x40};
    u8 dmac[6] = {0x00, 0x0f, 0x53, 0xa3, 0xea, 0x41};  // enp1s0f1's MAC

    sock.bind(ntohl(local_ip), local_port);

    if (!sock.connect(ntohl(remote_ip), remote_port, dmac, smac)) {
        std::cerr << "Connection failed.\n";
        return 1;
    }

    std::string packet = "sa dunya0\n";

    int cnt = 0;
    while (true) {
        sleep(1);
        packet[packet.size() - 2] = cnt++;
        sock.send({reinterpret_cast<const std::byte*>(packet.data()), packet.size()});
    }
}
