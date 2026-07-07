#pragma once

#include "tcp/socket.h"
#include "ouch/OuchApplication.h"

namespace soup {
    constexpr int HEARTBEAT_MS = 1000;
    constexpr int RX_TIMEOUT_MS = 15000;

    constexpr char LOGIN_ACCEPTED = 'A';
    constexpr char LOGIN_REJECTED = 'J';
    constexpr char HEARTBEAT = 'H';
    constexpr char END_OF_SESSION = 'Z';
    constexpr char SEQUENCED_DATA = 'S';
    constexpr char UNSEQUENCED_DATA = 'U';

    enum class SessionState {
        Disconnected,
        LoggingIn,
        Active
    };

    class SoupSession {
    public:
        SoupSession(tcp::socket& sock, ouch::OuchApplication& app);

        bool login(std::string_view username, std::string_view password, std::string_view session, std::string_view seq);
        bool logout();

        bool send_unsequenced(std::span<const std::byte> ouch_payload);
        // bool send_unsequenced(tx_sgl sgl);
        // bool send_unsequenced(pkt_buf* buf);

        void poll();

        [[nodiscard]] bool is_logged_in() const;

    private:
        bool send_heartbeat();
        void handle_rx();
        void check_timers();

        tcp::socket& sock;
        ouch::OuchApplication& app;

        SessionState state = SessionState::Disconnected;

        std::byte fragment_buffer[65536]{};

        u64 last_rx_cycles = 0;
        u64 last_tx_cycles = 0;

        std::array<char, 10> session{};
        u64 seq_num = 0;
    };
}
