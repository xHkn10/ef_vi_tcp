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
        u32 recv_next = 1;

        void inject(tcp::socket& sock, u8 flags, std::span<const std::byte> payload = {}) {
            std::vector<std::byte> frame{TCP_TOTAL_HDR_SZ + payload.size()};
            auto* eth = reinterpret_cast<net::eth_hdr*>(frame.data());
            auto* ip = reinterpret_cast<net::ip_hdr*>(frame.data() + ETH_HDR_SZ);
            auto* tcp = reinterpret_cast<net::tcp_hdr*>(frame.data() + ETH_HDR_SZ + IP_HDR_SZ);
            eth->ethertype = to_net<u16>(0x0800);
            ip->s_addr = REMOTE_IP;
            ip->d_addr = LOCAL_IP;
            ip->len = to_net<u16>(IP_HDR_SZ + TCP_HDR_SZ + payload.size());
            tcp->seq_num = to_net(seq);
            tcp->ack_num = to_net(recv_next);
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
}

void resource_checks(tcp::socket& sock) {
    CHECK(sock.test_ctx().tx_free_stk.size() + sock.test_tcb().tx_unacked.size() == io::N_TX_BUFS);
    CHECK(sock.test_ctx().rx_free_stk.size() + sock.test_tcb().rx_out_of_order.size() == io::N_RX_BUFS);
}

void test_handshake() {
    io::test::test_reset();

    tcp::socket sock;
    fake_peer peer;

    auto establish = [&] {
        if (!sock.bind(LOCAL_IP, LOCAL_PORT))
            return false;
        peer.inject(sock, SYN_FLAG | ACK_FLAG);
        return sock.connect(REMOTE_IP, REMOTE_PORT, dummy_mac, dummy_mac);
    };

    CHECK(establish());

    auto& cap = io::test::test_captured();

    CHECK(cap.size() == 2);
    CHECK(tcp_of(cap.front())->control == SYN_FLAG);
    CHECK(tcp_of(cap[1])->control == ACK_FLAG);
    CHECK(from_net(tcp_of(cap[1])->seq_num) == 1);
    CHECK(from_net(tcp_of(cap[1])->ack_num) == peer.seq);

    resource_checks(sock);
}

void test_rst_during_handshake() {
    io::test::test_reset();

    tcp::socket sock;
    fake_peer peer;

    CHECK(sock.bind(LOCAL_IP, LOCAL_PORT));
    peer.inject(sock, RST_FLAG);
    CHECK(!sock.connect(REMOTE_IP, REMOTE_PORT, dummy_mac, dummy_mac));
    CHECK(sock.test_tcb().state == tcp::TcpState::CLOSED);

    resource_checks(sock);
}


void test_active_close1() {
    io::test::test_reset();

    tcp::socket sock;
    fake_peer peer;
    auto cap = io::test::test_captured();


    sock.bind(LOCAL_IP, LOCAL_PORT);
    peer.inject(sock, SYN_FLAG | ACK_FLAG);
    sock.connect(REMOTE_IP, REMOTE_PORT, dummy_mac, dummy_mac);

    sock.close();

    CHECK(cap.size() == 3);
    CHECK(tcp_of(cap.back())->control == (FIN_FLAG | ACK_FLAG));
    CHECK(sock.test_tcb().state == tcp::TcpState::FIN_WAIT1);

    peer.inject(sock, FIN_FLAG | ACK_FLAG);

    sock.receive_available();

    CHECK(sock.test_tcb().state == tcp::TcpState::CLOSING);

    peer.inject(sock, ACK_FLAG);

    CHECK(sock.test_tcb().state == tcp::TcpState::TIME_WAIT);

    resource_checks(sock);
}


void test_active_close2() {
    io::test::test_reset();

    tcp::socket sock;
    fake_peer peer;
    auto cap = io::test::test_captured();

    sock.bind(LOCAL_IP, LOCAL_PORT);
    peer.inject(sock, SYN_FLAG | ACK_FLAG);
    sock.connect(REMOTE_IP, REMOTE_PORT, dummy_mac, dummy_mac);

    sock.close();


    CHECK(cap.size() == 3);
    CHECK(tcp_of(cap.back())->control == (FIN_FLAG | ACK_FLAG));
    CHECK(sock.test_tcb().state == tcp::TcpState::FIN_WAIT1);

    peer.inject(sock, ACK_FLAG);

    sock.receive_available();

    CHECK(sock.test_tcb().state == tcp::TcpState::FIN_WAIT2);

    peer.inject(sock, FIN_FLAG | ACK_FLAG);

    sock.receive_available();

    CHECK(sock.test_tcb().state == tcp::TcpState::TIME_WAIT);

    resource_checks(sock);
}

void test_passive_close() {
    io::test::test_reset();

    tcp::socket sock;
    fake_peer peer;
    auto& cap = io::test::test_captured();

    sock.bind(LOCAL_IP, LOCAL_PORT);
    peer.inject(sock, SYN_FLAG | ACK_FLAG);
    sock.connect(REMOTE_IP, REMOTE_PORT, dummy_mac, dummy_mac);

    peer.inject(sock, FIN_FLAG | ACK_FLAG);
    sock.receive_available();

    CHECK(sock.test_tcb().state == tcp::TcpState::CLOSE_WAIT);
    CHECK(tcp_of(cap.back())->control == ACK_FLAG);
    CHECK(cap.size() == 3); // SYN, SYN ACK, pure ACK of the FIN received

    sock.close();

    CHECK(sock.test_tcb().state == tcp::TcpState::LAST_ACK);
    CHECK(tcp_of(cap.back())->control == (FIN_FLAG | ACK_FLAG));

    peer.inject(sock, ACK_FLAG);
    sock.receive_available();

    CHECK(sock.test_tcb().state == tcp::TcpState::CLOSED);

    resource_checks(sock);
}

void test_active_abort() {
    io::test::test_reset();

    tcp::socket sock;
    fake_peer peer;
    auto& cap = io::test::test_captured();

    sock.bind(LOCAL_IP, LOCAL_PORT);
    peer.inject(sock, SYN_FLAG | ACK_FLAG);
    sock.connect(REMOTE_IP, REMOTE_PORT, dummy_mac, dummy_mac);

    sock.abort();

    CHECK(cap.size() == 3);
    CHECK(tcp_of(cap.back())->control == RST_FLAG);
    CHECK(sock.test_tcb().state == tcp::TcpState::CLOSED);
}

int main() {
    test_handshake();
    test_rst_during_handshake();
    test_passive_close();
    test_active_close1();
    test_active_close2();
    test_active_abort();
    printf("%d errors\n", g_failures);
}
