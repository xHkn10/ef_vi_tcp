#pragma once

#include <string_view>

#include "types.h"
#include "ouch/Application.h"
#include "soup/Session.h"
#include "tcp/socket.h"

struct AccountConfig {
    std::array<u8, 6> smac, dmac;
    u32 local_ip, remote_ip;
    u16 local_port, remote_port;
    std::string_view username, pass;
};

enum class Phase {
    Idle,
    Connecting,
    LoggingIn,
    Active,
    Stopped
};

struct Account {
    tcp::socket sock;
    ouch::Application app;
    soup::Session session{sock, app};
    Phase phase;
    AccountConfig cfg;
};
