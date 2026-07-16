#include "tcp/socket.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <iostream>
#include <memory>

#include "engine/Engine.h"

constexpr int N = 1;
std::string_view username = "hakan";
std::string_view pass = "PASS";

int main() {
    int ifindex = if_nametoindex(io::INTERFACE_NAME);
    if (ifindex == 0) {
        LOG_ERROR("Failed to find interface %s", io::INTERFACE_NAME);
        return 1;
    }

    auto engine = std::make_unique<engine::Engine<N>>();

    auto& cfg = engine->accounts[0].cfg;

    cfg = {
        .smac = {},
        .dmac = {},
        .local_ip = 0,
        .remote_ip = 0,
        .local_port = 0,
        .remote_port = 0,
        .username = username,
        .pass = pass
    };

    bool res = engine->accounts[0].sock.bind(cfg.local_ip, cfg.local_port, cfg.remote_ip, cfg.remote_port, cfg.dmac, cfg.smac);
    if (!res)
        return 1;

    engine->run();
}
