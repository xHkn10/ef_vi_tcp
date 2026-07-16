#pragma once

#include <charconv>
#include <random>
#include "types.h"
#include "io/backend_test.h"
#include "soup/Session.h"
#include "tcp/socket.h"

inline int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++g_failures; \
fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

inline std::random_device rd;
inline std::mt19937_64 gen{rd()};

constexpr u32 LOCAL_IP{1}, REMOTE_IP{2};
constexpr u16 LOCAL_PORT{1}, REMOTE_PORT{2};

inline u8 dummy_mac[6]{};

struct fake_tcp_peer {
    u32 seq = 1000;

    void inject(tcp::socket& sock, u8 flags, u32 recv_next = 0, std::span<const std::byte> payload = {}) {
        std::vector<std::byte> frame{TCP_TOTAL_HDR_SZ + payload.size()};

        auto* eth = reinterpret_cast<net::eth_hdr*>(frame.data());
        auto* ip = reinterpret_cast<net::ip_hdr*>(frame.data() + ETH_HDR_SZ);
        auto* tcp = reinterpret_cast<net::tcp_hdr*>(frame.data() + ETH_HDR_SZ + IP_HDR_SZ);

        eth->ethertype = to_net<u16>(0x0800);
        ip->s_addr = REMOTE_IP;
        ip->d_addr = LOCAL_IP;
        ip->len = to_net<u16>(IP_HDR_SZ + TCP_HDR_SZ + payload.size());
        tcp->seq_num = to_net(seq);
        tcp->ack_num = to_net(recv_next == 0 ? sock.test_tcb().SND_NXT : recv_next);
        tcp->control = flags;
        tcp->doffset_reserved = 0x50;

        std::memcpy(frame.data() + TCP_TOTAL_HDR_SZ, payload.data(), payload.size());

        seq += payload.size() + !!(flags & (SYN_FLAG | FIN_FLAG));

        io::test::test_inject(sock.test_ctx(), frame);
    }

    void inject_data(tcp::socket& sock, std::span<const std::byte> payload) {
        while (!payload.empty()) {
            const u32 chunk = std::min<size_t>(payload.size(), TCP_MAX_PAYLOAD_SZ);
            inject(sock, ACK_FLAG, 0, payload.first(chunk));
            payload = payload.subspan(chunk);
        }
    }
};

struct fake_soup_peer {
    std::string_view session_name = "dümen";
    u64 cur_seq = 1;

    void accept_login(tcp::socket& sock, fake_tcp_peer& tcp_peer, u64 seq = 1) {
        std::array<std::byte, soup::LOGIN_ACCEPTED_SZ + 2> la{};
        *reinterpret_cast<u16*>(la.data()) = to_net<u16>(soup::LOGIN_ACCEPTED_SZ);
        la[2] = static_cast<std::byte>(soup::LOGIN_ACCEPTED);

        std::memset(la.data() + 3, ' ', soup::MAX_SESSION_SZ + soup::MAX_SEQUENCE_SZ);
        std::memcpy(la.data() + 3 + soup::MAX_SESSION_SZ - session_name.size(), session_name.data(), session_name.size());

        char seq_text[20];
        auto [end, _] = std::to_chars(seq_text, seq_text + sizeof(seq_text), seq);

        cur_seq = seq;

        std::memcpy(la.data() + la.size() - (end - seq_text), seq_text, end - seq_text);

        tcp_peer.inject_data(sock, la);
    }

    void reject_login(tcp::socket& sock, fake_tcp_peer& tcp_peer) {
        std::array<std::byte, soup::LOGIN_REJECTED_SZ + 2> la{};
        *reinterpret_cast<u16*>(la.data()) = to_net<u16>(soup::LOGIN_REJECTED_SZ);
        la[2] = static_cast<std::byte>(soup::LOGIN_REJECTED);
        la[3] = static_cast<std::byte>('A');
        tcp_peer.inject_data(sock, la);
    }

    void send_sequenced(tcp::socket& sock, fake_tcp_peer& tcp_peer) {
        ouch::order_accepted_msg msg{ouch::ORDER_ACCEPTED_VAL};
        std::array<std::byte, sizeof(msg) + 3> ss{};

        *reinterpret_cast<u16*>(ss.data()) = to_net<u16>(sizeof(msg) + 1);
        ss[2] = static_cast<std::byte>(soup::SEQUENCED_DATA);
        std::memcpy(ss.data() + 3, &msg, sizeof(msg));

        ++cur_seq;

        tcp_peer.inject_data(sock, ss);
    }
};

inline net::tcp_hdr* tcp_of(std::vector<std::byte>& packet) {
    return reinterpret_cast<net::tcp_hdr*>(packet.data() + ETH_HDR_SZ + IP_HDR_SZ);
}
inline net::ip_hdr* ip_of(std::vector<std::byte>& packet) {
    return reinterpret_cast<net::ip_hdr*>(packet.data() + ETH_HDR_SZ);
}
inline std::span<std::byte> payload_of(std::vector<std::byte>& packet) {
    const int tcp_hdr_len = (tcp_of(packet)->doffset_reserved >> 4) * 4;
    const u32 payload_len = from_net(ip_of(packet)->len) - IP_HDR_SZ - tcp_hdr_len;
    return {reinterpret_cast<std::byte*>(tcp_of(packet)) + tcp_hdr_len, payload_len};
}

inline void drain(tcp::socket& sock) {
    while (!io::test::g_rx_pending.empty() || !io::test::g_tx_done.empty())
        sock.poll();
}

#define resource_checks \
    sock.poll(); \
    CHECK(sock.test_ctx().tx_free_stk.size() + sock.test_tcb().tx_unacked.size() == io::N_TX_BUFS); \
    int ready_cnt = 0; \
    for (auto* pb = sock.test_tcb().rx_ready_head; pb; pb = pb->meta.nxt) \
        ++ready_cnt; \
    CHECK(sock.test_ctx().rx_free_stk.size() + sock.test_tcb().rx_out_of_order.size() + io::test::g_rx_avail.size() + io::test::g_rx_pending.size() + ready_cnt == io::N_RX_BUFS);

#define establish_tcp \
    io::test::test_reset(); \
    tcp::socket sock; \
    fake_tcp_peer tcp_peer; \
    auto& cap = io::test::g_sent_captured; \
    CHECK(sock.bind(LOCAL_IP, LOCAL_PORT, REMOTE_IP, REMOTE_PORT, dummy_mac, dummy_mac)); \
    CHECK(sock.connect()); \
    tcp_peer.inject(sock, SYN_FLAG | ACK_FLAG); \
    sock.poll(); \
    CHECK(sock.test_tcb().state == tcp::fsm::ESTABLISHED); \
    CHECK(cap.size() == 2);

#define establish_soup \
    establish_tcp \
    ouch::Application app; \
    soup::Session sess{sock, app}; \
    fake_soup_peer soup_peer; \
    CHECK(sess.test_session_state() == soup::SessionState::Disconnected); \
    sess.login(username, pass); \
    CHECK(sess.test_session_state() == soup::SessionState::LoggingIn); \
    soup_peer.accept_login(sock, tcp_peer); \
    sock.poll(); \
    sess.poll(); \
    CHECK(sess.is_logged_in()); \
    CHECK(sess.test_seq_num() == 1); \


inline auto make_byte_span = [](auto& container) {
    return std::span{reinterpret_cast<std::byte*>(container.data()), container.size()};
};
