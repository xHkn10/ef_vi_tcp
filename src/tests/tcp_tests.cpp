#include "io/backend_test.h"
#include "test_utils.h"

void test_handshake() {
    establish_tcp

    CHECK(cap.size() == 2);
    CHECK(tcp_of(cap.front())->control == SYN_FLAG);
    CHECK(tcp_of(cap[1])->control == ACK_FLAG);
    CHECK(from_net(tcp_of(cap[1])->seq_num) == 1);
    CHECK(from_net(tcp_of(cap[1])->ack_num) == tcp_peer.seq);

    resource_checks
}

void test_rst_during_handshake() {
    io::test::test_reset();

    tcp::socket sock;
    fake_tcp_peer tcp_peer;

    CHECK(sock.bind(LOCAL_IP, LOCAL_PORT, REMOTE_IP, REMOTE_PORT, dummy_mac, dummy_mac));
    CHECK(sock.connect());

    tcp_peer.inject(sock, RST_FLAG);
    sock.poll();

    CHECK(sock.test_tcb().state == tcp::fsm::CLOSED);

    resource_checks
}


void test_active_close1() {
    establish_tcp

    CHECK(sock.close());

    CHECK(cap.size() == 3);
    CHECK(tcp_of(cap.back())->control == (FIN_FLAG | ACK_FLAG));
    CHECK(sock.test_tcb().state == tcp::fsm::FIN_WAIT1);

    tcp_peer.inject(sock, FIN_FLAG | ACK_FLAG, 1);
    sock.poll();

    CHECK(sock.test_tcb().state == tcp::fsm::CLOSING);

    tcp_peer.inject(sock, ACK_FLAG);
    sock.poll();

    CHECK(sock.test_tcb().state == tcp::fsm::TIME_WAIT);

    resource_checks
}

void test_active_close2() {
    establish_tcp

    CHECK(sock.close());

    CHECK(cap.size() == 3);
    CHECK(tcp_of(cap.back())->control == (FIN_FLAG | ACK_FLAG));
    CHECK(sock.test_tcb().state == tcp::fsm::FIN_WAIT1);

    tcp_peer.inject(sock, ACK_FLAG);
    sock.poll();

    CHECK(sock.test_tcb().state == tcp::fsm::FIN_WAIT2);

    tcp_peer.inject(sock, FIN_FLAG | ACK_FLAG);
    sock.poll();

    CHECK(sock.test_tcb().state == tcp::fsm::TIME_WAIT);

    resource_checks
}

void test_passive_close() {
    establish_tcp

    tcp_peer.inject(sock, FIN_FLAG | ACK_FLAG);
    sock.poll();

    CHECK(sock.test_tcb().state == tcp::fsm::CLOSE_WAIT);
    CHECK(cap.size() == 3);
    CHECK(tcp_of(cap.back())->control == ACK_FLAG);

    CHECK(sock.close());

    CHECK(sock.test_tcb().state == tcp::fsm::LAST_ACK);
    CHECK(tcp_of(cap.back())->control == (FIN_FLAG | ACK_FLAG));

    tcp_peer.inject(sock, ACK_FLAG);
    sock.poll();

    CHECK(sock.test_tcb().state == tcp::fsm::CLOSED);

    CHECK(cap.size() == 4);

    resource_checks
}

void test_active_abort() {
    establish_tcp

    CHECK(sock.abort());

    CHECK(cap.size() == 3);
    CHECK(tcp_of(cap.back())->control == RST_FLAG);
    CHECK(sock.test_tcb().state == tcp::fsm::CLOSED);

    resource_checks
}

void test_inplace_send() {
    establish_tcp

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
    establish_tcp

    std::array<std::byte, 50000> big_msg;
    char cur = 0;
    for (auto& b : big_msg)
        b = static_cast<std::byte>(cur++);

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
    establish_tcp

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

void test_receive() {
    establish_tcp

    std::string msg = "sa dunya!";
    tcp_peer.inject(sock, ACK_FLAG, 0, make_byte_span(msg));

    sock.poll();

    io::cycle_timer::elapse(DELAYED_ACK_TIMEOUT_MILLISECONDS - 1);
    sock.poll();
    CHECK(cap.size() == 2);
    io::cycle_timer::elapse(1);
    sock.poll();
    CHECK(cap.size() == 3);

    std::string rcv_buffer(10, 0);
    const int rcvd_sz = sock.receive(make_byte_span(rcv_buffer));
    CHECK(rcvd_sz == msg.size());

    CHECK(std::memcmp(msg.data(), rcv_buffer.data(), msg.size()) == 0);

    resource_checks
}


void test_ooo_receive() {
    establish_tcp

    std::string msg = "hakan akbıyık bulltech";

    u32 start = tcp_peer.seq;
    u32 rcv_nxt = sock.test_tcb().RCV_NXT;
    int needed_sz = cap.size();
    for (int i = 0; i < msg.size(); ++i) {
        std::byte b[1] = {static_cast<std::byte>(msg[msg.size() - 1 - i])};

        tcp_peer.seq = start + msg.size() - 1 - i;
        tcp_peer.inject(sock, ACK_FLAG, 0, b);

        sock.poll();

        if (i + 1 != msg.size()) {
            CHECK(sock.receive_available().n_bytes == 0);
            CHECK(cap.size() == ++needed_sz);
            CHECK(sock.receive_available().n_bytes == 0);
            CHECK(from_net(tcp_of(cap.back())->ack_num) == rcv_nxt);
        }
    }

    sock.poll();

    std::string rcvd_msg(msg.size(), 0);

    CHECK(sock.receive(make_byte_span(rcvd_msg)) == msg.size());

    CHECK(std::memcmp(msg.data(), rcvd_msg.data(), msg.size()) == 0);

    resource_checks
}

void test_rto() {
    establish_tcp

    std::array snd{0x01, 0x31, 0x10, 0xA, 0xB};
    u32 cur_seq = sock.test_tcb().SND_NXT;
    CHECK(sock.send(make_byte_span(snd)) == snd.size());

    int needed_sz = cap.size();

    for (int i = 0; i < 10; ++i) {
        io::cycle_timer::elapse(RETRANSMISSION_TIMEOUT_MILLISECONDS);
        sock.poll();

        CHECK(cap.size() == ++needed_sz);

        auto payload = payload_of(cap.back());
        CHECK(payload.size() == snd.size());
        CHECK(std::memcmp(payload.data(), snd.data(), snd.size()) == 0);

        CHECK(from_net(tcp_of(cap.back())->seq_num) == cur_seq);
    }

    resource_checks
}

void test_duplicate_send() {
    establish_tcp

    std::string msg = "hakan akbıyık bulltech stajyer";
    tcp_peer.inject(sock, ACK_FLAG, 0, make_byte_span(msg));
    sock.poll();
    CHECK(cap.size() == 2);
    io::cycle_timer::elapse(DELAYED_ACK_TIMEOUT_MILLISECONDS);
    sock.poll();
    CHECK(cap.size() == 3);

    for (int i = 0; i < 100; ++i) {
        tcp_peer.seq = 0;
        tcp_peer.inject(sock, ACK_FLAG, 0, make_byte_span(msg));
        sock.poll();
        CHECK(cap.size() == 4 + i);
        CHECK(from_net(tcp_of(cap.back())->ack_num) == sock.test_tcb().ISR + 1 + msg.size());
    }

    resource_checks
}

void test_erroneous_handshake() {
    io::test::test_reset();
    tcp::socket sock{};
    fake_tcp_peer tcp_peer{};

    sock.bind(LOCAL_IP, LOCAL_PORT, REMOTE_IP, REMOTE_PORT, dummy_mac, dummy_mac);
    sock.connect();

    tcp_peer.inject(sock, SYN_FLAG | ACK_FLAG, 100);
    sock.poll();

    CHECK(sock.test_tcb().state == tcp::fsm::SYN_SENT);
    
    resource_checks
}


void test_wraparound() {
    establish_tcp

    u32 start = UINT32_MAX - 10;
    sock.test_tcb().SND_NXT = start;
    sock.test_tcb().SND_UNA = start;

    sock.test_tcb().SND_NXT = start;

    std::string msg = "hakan akbıyık sa dunya";
    u32 msg_sz = msg.size();

    CHECK(cap.size() == 2);

    CHECK(sock.send(make_byte_span(msg)) == msg_sz);

    CHECK(cap.size() == 3);

    CHECK(from_net(tcp_of(cap.back())->seq_num) == start);

    CHECK(sock.test_tcb().SND_NXT == start + msg_sz);

    CHECK(sock.send(make_byte_span(msg)) == msg_sz);

    CHECK(cap.size() == 4);

    CHECK(from_net(tcp_of(cap.back())->seq_num) == start + msg_sz);

    resource_checks
}


void test_partial_ack_received() {
    establish_tcp

    std::string msg = "bla bla bla bla bla blabl alb alb bla";

    CHECK(sock.send(make_byte_span(msg)) == msg.size());

    const int partial_cutoff = 5;

    tcp_peer.inject(sock, ACK_FLAG, partial_cutoff);

    sock.poll();

    CHECK(cap.size() == 3);

    CHECK(sock.test_tcb().SND_UNA == partial_cutoff);

    io::cycle_timer::elapse(RETRANSMISSION_TIMEOUT_MILLISECONDS);

    sock.poll();

    CHECK(cap.size() == 4);

    // VERY IMPORTANT NOTE:
    // There is no need to strictly retransmit starting from SND_UNA.
    // In case of a partial ACK where it slices a pkt_buf payload,
    // strictly retransmitting from SND_UNA would require a std::memmove.
    // Retransmitting from before SND_UNA is OK and RFC compliant.

    const auto lhs = payload_of(cap[cap.size() - 2]);
    const auto rhs = payload_of(cap[cap.size() - 1]);

    CHECK(lhs.size() == rhs.size());
    CHECK(std::memcmp(lhs.data(), rhs.data(), lhs.size()) == 0);

    resource_checks
}

void test_huge_send_recv_simultaneously() {
    establish_tcp

    constexpr int n_bytes = 1'000'000;

    auto make_random_bytes = [] {
        std::uniform_int_distribution dist(0, 255);
        std::vector<char> buffer(n_bytes);
        for (auto& b : buffer)
            b = static_cast<char>(dist(gen));
        return buffer;
    };

    std::vector<char> rcv_data = make_random_bytes();
    std::vector<char> snd_data = make_random_bytes();
    auto rcv_span = make_byte_span(rcv_data);
    auto snd_span = make_byte_span(snd_data);

    std::uniform_int_distribution dist{1, UINT16_MAX};

    int sock_sent = 0;
    int peer_sent = 0;
    while (sock_sent < n_bytes || peer_sent < n_bytes) {
        const int sock_chunk = std::min(n_bytes - sock_sent, dist(gen));
        const int peer_chunk = std::min(n_bytes - peer_sent, dist(gen));

        CHECK(sock.send(snd_span.subspan(sock_sent, sock_chunk)) == sock_chunk);
        sock_sent += sock_chunk;

        if (peer_chunk > 0)
            tcp_peer.inject_data(sock, rcv_span.subspan(peer_sent, peer_chunk));
        else
            tcp_peer.inject(sock, ACK_FLAG);

        drain(sock);

        std::vector<char> rcv_buf_check(peer_chunk);

        CHECK(sock.receive(make_byte_span(rcv_buf_check)) == peer_chunk);
        CHECK(std::memcmp(rcv_buf_check.data(), rcv_data.data() + peer_sent, peer_chunk) == 0);

        peer_sent += peer_chunk;
    }

    resource_checks
}

void test_tcp_time_wait_transition() {
    establish_tcp

    CHECK(sock.close());
    CHECK(sock.test_tcb().state == tcp::fsm::FIN_WAIT1);

    tcp_peer.inject(sock, FIN_FLAG | ACK_FLAG);

    sock.poll();
    CHECK(sock.test_tcb().state == tcp::fsm::TIME_WAIT);

    io::cycle_timer::elapse(TIME_WAIT_MILLISECONDS);

    sock.poll();

    CHECK(sock.is_closed());

    resource_checks
}

void rx_ooo_insert_test() {
    io::test::test_reset();
    tcp::socket sock;
    auto& bufs = sock.test_ctx().rx_pkt_bufs;
    tcp::rx_ooo_list list;
    auto mk = [&](int i, u32 seq) { bufs[i]->meta = {{}, nullptr, seq, 0}; return bufs[i]; };

    CHECK(list.insert(mk(0, 10)));
    CHECK(list.insert(mk(1, 20)));
    CHECK(list.insert(mk(2, 30)));
    CHECK(list.insert(mk(3, 25)));

    CHECK(!list.insert(mk(4, 25)));
    CHECK(!list.insert(mk(5, 20)));
    CHECK(!list.insert(mk(6, 30)));

    CHECK(list.insert(mk(7, 35)));

    for (u32 e : {10u, 20u, 25u, 30u, 35u}) {
        auto* p = list.pop_front();
        CHECK(p && p->meta.seq == e);
    }

    CHECK(list.empty());
}
