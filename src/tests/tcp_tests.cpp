#include <tcp/socket.h>

#include "io/backend_test.h"

int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++g_failures; \
fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace {
    constexpr u32 LOCAL_IP{1}, REMOTE_IP{2};
    constexpr u16 LOCAL_PORT{1}, REMOTE_PORT{2};

    u8 dummy_mac[6]{};

    struct fake_peer {
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

    };

    net::tcp_hdr* tcp_of(std::vector<std::byte>& packet) {
        return reinterpret_cast<net::tcp_hdr*>(packet.data() + ETH_HDR_SZ + IP_HDR_SZ);
    }
    net::ip_hdr* ip_of(std::vector<std::byte>& packet) {
        return reinterpret_cast<net::ip_hdr*>(packet.data() + ETH_HDR_SZ);
    }
    std::span<std::byte> payload_of(std::vector<std::byte>& packet) {
        const int tcp_hdr_len = (tcp_of(packet)->doffset_reserved >> 4) * 4;
        const u32 payload_len = from_net(ip_of(packet)->len) - IP_HDR_SZ - tcp_hdr_len;
        return {reinterpret_cast<std::byte*>(tcp_of(packet)) + tcp_hdr_len, payload_len};
    }
}

#define resource_checks \
    sock.poll(); \
    CHECK(sock.test_ctx().tx_free_stk.size() + sock.test_tcb().tx_unacked.size() == io::N_TX_BUFS); \
    int ready_cnt = 0; \
    for (auto* pb = sock.test_tcb().rx_ready_head; pb; pb = pb->meta.nxt) \
        ++ready_cnt; \
    CHECK(sock.test_ctx().rx_free_stk.size() + sock.test_tcb().rx_out_of_order.size() + io::test::g_rx_avail.size() + io::test::g_rx_pending.size() + ready_cnt == io::N_RX_BUFS);

#define establish \
    CHECK(sock.bind(LOCAL_IP, LOCAL_PORT, REMOTE_IP, REMOTE_PORT, dummy_mac, dummy_mac)); \
    CHECK(sock.connect()); \
    peer.inject(sock, SYN_FLAG | ACK_FLAG); \
    sock.poll(); \
    CHECK(sock.test_tcb().state == tcp::fsm::ESTABLISHED);

void test_handshake() {
    io::test::test_reset();

    tcp::socket sock;
    fake_peer peer;
    auto& cap = io::test::g_sent_captured;

    {
        resource_checks
    }

    establish

    CHECK(cap.size() == 2);
    CHECK(tcp_of(cap.front())->control == SYN_FLAG);
    CHECK(tcp_of(cap[1])->control == ACK_FLAG);
    CHECK(from_net(tcp_of(cap[1])->seq_num) == 1);
    CHECK(from_net(tcp_of(cap[1])->ack_num) == peer.seq);

    resource_checks
}

void test_rst_during_handshake() {
    io::test::test_reset();

    tcp::socket sock;
    fake_peer peer;

    CHECK(sock.bind(LOCAL_IP, LOCAL_PORT, REMOTE_IP, REMOTE_PORT, dummy_mac, dummy_mac));
    CHECK(sock.connect());

    peer.inject(sock, RST_FLAG);
    sock.poll();

    CHECK(sock.test_tcb().state == tcp::fsm::CLOSED);

    resource_checks
}


void test_active_close1() {
    io::test::test_reset();

    tcp::socket sock;
    fake_peer peer;
    auto& cap = io::test::g_sent_captured;

    establish

    CHECK(sock.close());

    CHECK(cap.size() == 3);
    CHECK(tcp_of(cap.back())->control == (FIN_FLAG | ACK_FLAG));
    CHECK(sock.test_tcb().state == tcp::fsm::FIN_WAIT1);

    peer.inject(sock, FIN_FLAG | ACK_FLAG, 1);
    sock.poll();

    CHECK(sock.test_tcb().state == tcp::fsm::CLOSING);

    peer.inject(sock, ACK_FLAG);
    sock.poll();

    CHECK(sock.test_tcb().state == tcp::fsm::TIME_WAIT);

    resource_checks
}

void test_active_close2() {
    io::test::test_reset();

    tcp::socket sock;
    fake_peer peer;
    auto& cap = io::test::g_sent_captured;

    establish

    CHECK(sock.close());

    CHECK(cap.size() == 3);
    CHECK(tcp_of(cap.back())->control == (FIN_FLAG | ACK_FLAG));
    CHECK(sock.test_tcb().state == tcp::fsm::FIN_WAIT1);

    peer.inject(sock, ACK_FLAG);
    sock.poll();

    CHECK(sock.test_tcb().state == tcp::fsm::FIN_WAIT2);

    peer.inject(sock, FIN_FLAG | ACK_FLAG);
    sock.poll();

    CHECK(sock.test_tcb().state == tcp::fsm::TIME_WAIT);

    resource_checks
}

void test_passive_close() {
    io::test::test_reset();

    tcp::socket sock;
    fake_peer peer;
    auto& cap = io::test::g_sent_captured;

    establish

    peer.inject(sock, FIN_FLAG | ACK_FLAG);
    sock.poll();

    CHECK(sock.test_tcb().state == tcp::fsm::CLOSE_WAIT);
    CHECK(cap.size() == 3);
    CHECK(tcp_of(cap.back())->control == ACK_FLAG);

    CHECK(sock.close());

    CHECK(sock.test_tcb().state == tcp::fsm::LAST_ACK);
    CHECK(tcp_of(cap.back())->control == (FIN_FLAG | ACK_FLAG));

    peer.inject(sock, ACK_FLAG);
    sock.poll();

    CHECK(sock.test_tcb().state == tcp::fsm::CLOSED);

    CHECK(cap.size() == 4);

    resource_checks
}

void test_active_abort() {
    io::test::test_reset();

    tcp::socket sock;
    fake_peer peer;
    auto& cap = io::test::g_sent_captured;

    establish

    CHECK(sock.abort());

    CHECK(cap.size() == 3);
    CHECK(tcp_of(cap.back())->control == RST_FLAG);
    CHECK(sock.test_tcb().state == tcp::fsm::CLOSED);

    resource_checks
}

void test_inplace_send() {
    io::test::test_reset();

    tcp::socket sock;
    fake_peer peer;
    auto& cap = io::test::g_sent_captured;

    establish

    auto* pb = sock.get_tx_buf();
    std::string_view msg = "sa dunya";
    std::memcpy(pb->meta.payload.data(), msg.data(), msg.size());
    pb->meta.payload = {pb->dma_buf, msg.size()};

    CHECK(sock.send(pb));

    CHECK(cap.size() == 3);

    const int ip_len_field = from_net(ip_of(cap.back())->len);
    const int tcp_hdr_len = (tcp_of(cap.back())->doffset_reserved >> 4) * 4;
    const int rcvd_sz = ip_len_field - IP_HDR_SZ - tcp_hdr_len;

    CHECK(rcvd_sz == msg.size());

    auto payload = payload_of(cap.back());
    auto is_eq = std::memcmp(msg.data(), payload.data(), msg.size());
    CHECK(is_eq == 0);
}

void test_big_span_send() {
    io::test::test_reset();

    tcp::socket sock;
    fake_peer peer;
    auto& cap = io::test::g_sent_captured;

    std::array<std::byte, 50000> big_msg;
    char cur = 0;
    for (auto& b : big_msg)
        b = static_cast<std::byte>(cur++);

    establish

    CHECK(sock.send(big_msg));

    {
        int rcvd_sz = 0;
        for (auto& packet : cap | std::views::drop(2))
            rcvd_sz += payload_of(packet).size();

        CHECK(rcvd_sz == big_msg.size());
    }

    {
        int big_msg_p = 0;
        int is_diff = 0;

        for (auto& packet : cap | std::views::drop(2))
            for (auto b : payload_of(packet))
                is_diff += b != big_msg[big_msg_p++];

        CHECK(is_diff == 0);
    }

    resource_checks
}

void test_tx_sgl_send() {
    io::test::test_reset();

    tcp::socket sock;
    fake_peer peer;
    auto& cap = io::test::g_sent_captured;

    const int big_msg_sz = 50000;
    std::array<std::byte, big_msg_sz> big_msg;
    {
        unsigned char cur = 0;
        for (auto& b : big_msg)
            b = static_cast<std::byte>(cur++);
    }

    io::tx_sgl sgl = sock.get_tx_sgl(big_msg_sz);

    {
        int big_msg_p = 0;
        int left = big_msg_sz;
        for (auto* pb : sgl.segments) {
            const int n = std::min(TCP_MAX_PAYLOAD_SZ, left);
            std::memcpy(pb->meta.payload.data(), big_msg.data() + big_msg_p, n);
            pb->set_payload_sz(n);
            left -= n;
            big_msg_p += n;
        }
    }

    establish

    CHECK(sock.send(std::move(sgl)));

    {
        int rcvd_sz = 0;
        for (auto& packet : cap | std::views::drop(2))
            rcvd_sz += payload_of(packet).size();

        CHECK(rcvd_sz == big_msg.size());
    }

    {
        int big_msg_p = 0;
        int is_diff = 0;

        for (auto& packet : cap | std::views::drop(2))
            for (auto b : payload_of(packet))
                is_diff += b != big_msg[big_msg_p++];

        CHECK(is_diff == 0);
    }

    resource_checks
}

int main() {
    test_handshake();
    test_rst_during_handshake();
    test_passive_close();
    test_active_close1();
    test_active_close2();
    test_active_abort();
    test_inplace_send();
    test_big_span_send();
    test_tx_sgl_send();
    printf("%d errors\n", g_failures);
}
