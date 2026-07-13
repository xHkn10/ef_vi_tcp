#include "tcp/socket.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <iostream>

int main() {
    int ifindex = if_nametoindex("enp1s0f0");
    if (ifindex == 0) {
        LOG_ERROR("Failed to find interface enp1s0f0");
        return 1;
    }


}
