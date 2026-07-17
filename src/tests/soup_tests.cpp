#include "test_utils.h"
#include "soup/Session.h"

void test_soup_login() {
    establish_soup
    resource_checks
}

void test_soup_login_rej() {
    establish_tcp
    ouch::Application app;
    soup::Session sess{sock, app};
    fake_soup_peer soup_peer;
    CHECK(sess.test_session_state() == soup::SessionState::Disconnected);
    sess.login(username, pass);
    CHECK(sess.test_session_state() == soup::SessionState::LoggingIn);

    soup_peer.reject_login(sock, tcp_peer);

    sock.poll(); sess.poll();

    CHECK(sess.is_disconnected());

    resource_checks
}

void test_soup_logout() {
    establish_soup

    sess.logout();
    CHECK(sess.is_logged_out());

    CHECK(sock.test_tcb().state != tcp::fsm::ESTABLISHED);

    CHECK(tcp_of(cap.back())->control == (FIN_FLAG | ACK_FLAG));

    resource_checks
}

void test_soup_seq_num() {
    establish_soup

    constexpr int N = 10;

    for (int i = 0; i < N; ++i) {
        CHECK(sess.test_seq_num() == 1 + i);
        soup_peer.send_sequenced(sock, tcp_peer);
        sock.poll(); sess.poll();
    }

    resource_checks
}

void test_soup_resume_login() {
    establish_soup

    soup_peer.send_sequenced(sock, tcp_peer);
    sock.poll(); sess.poll();

    CHECK(sess.test_seq_num() == 2);

    sock.abort();
    sess.reset();

    CHECK(sock.is_closed());
    CHECK(sess.is_disconnected());
    CHECK(sock.connect());

    tcp_peer.inject(sock, SYN_FLAG | ACK_FLAG);
    sock.poll();

    CHECK(sock.is_established());

    CHECK(sess.login(username, pass));

    const auto soup = payload_of(cap.back());

    CHECK(soup.size() == 49);
    CHECK(static_cast<char>(soup[2]) == 'L');
    CHECK(std::memcmp(soup.data() + 19, "    dümen", 10) == 0);
    CHECK(static_cast<char>(soup[48]) == '2');
    CHECK(static_cast<char>(soup[47]) == ' ');
}

void test_soup_fragmentation() {
    establish_soup

    auto seq_packet = get_sequenced_packet();

    constexpr int N = 10;

    for (int i = 1; i <= N; ++i) {
        std::span<std::byte> spn = seq_packet;

        while (spn.size()) {
            const int rnd = std::uniform_int_distribution{0, std::min((int)spn.size(), 5)}(gen);
            tcp_peer.inject_data(sock, spn.first(rnd));
            spn = spn.subspan(rnd);

            sock.poll(); sess.poll();

            CHECK(sess.test_seq_num() == i + !spn.size());
        }

        CHECK(sess.test_seq_num() == i + 1);
    }

    CHECK(sess.test_seq_num() == N + 1);
}

void test_soup_heartbeat() {
    establish_soup

    const int init_cap_sz = cap.size();

    io::cycle_timer::elapse(soup::HEARTBEAT_MS);

    sock.poll(); sess.poll();

    CHECK(cap.size() == init_cap_sz + 2);

    auto heart = payload_of(cap.back());

    CHECK(heart.size() == 3);
    CHECK(*reinterpret_cast<u16*>(heart.data()) == to_net<u16>(1));
    CHECK(heart[2] == static_cast<std::byte>('R'));

    resource_checks
}

void test_soup_end_of_session() {
    establish_soup

    soup_peer.end_session(sock, tcp_peer);

    sock.poll(); sess.poll();

    CHECK(sess.is_logged_out());
    CHECK(!sess.test_have_session());

    resource_checks
}

void test_soup_multiple_sequenced_in_one_segment() {
    establish_soup

    constexpr int N = 10;
    constexpr int msg_sz = sizeof(ouch::order_rejected_msg);

    std::array<std::byte, N * (3 + msg_sz)> big_msg{};
    for (int i = 0; i < N; ++i) {
        const int cur_off = i * (3 + msg_sz);
        *reinterpret_cast<u16*>(big_msg.data() + cur_off) = to_net<u16>(msg_sz + 1);
        big_msg[cur_off + 2] = static_cast<std::byte>('S');
        big_msg[cur_off + 3] = static_cast<std::byte>(ouch::ORDER_REJECTED_VAL);
    }

    tcp_peer.inject_data(sock, big_msg);

    sock.poll(); sess.poll();

    CHECK(sess.test_seq_num() == N + 1);

    resource_checks
}

void test_soup_unknown_type_skip() {
    establish_soup

    const auto seq_pkt = get_sequenced_packet();

    // unknown type + known type

    std::array<std::byte, 8 + sizeof(seq_pkt)> buf{};
    *reinterpret_cast<u16*>(buf.data()) = to_net<u16>(6);
    buf[2] = static_cast<std::byte>('Q');
    std::memcpy(buf.data() + 8, seq_pkt.data(), seq_pkt.size());

    tcp_peer.inject_data(sock, buf);
    sock.poll(); sess.poll();

    CHECK(sess.is_logged_in());
    CHECK(sess.test_seq_num() == 2);

    resource_checks
}

void test_soup_zero_length_fatal() {
    establish_soup

    std::array<std::byte, 3> bad{};

    tcp_peer.inject_data(sock, bad);
    sock.poll(); sess.poll();

    CHECK(sess.is_disconnected());
    CHECK(sock.is_closed());
    CHECK(tcp_of(cap.back())->control & RST_FLAG);

    resource_checks
}

void test_soup_relogin_after_reject() {
    establish_tcp
    ouch::Application app;
    soup::Session sess{sock, app};
    fake_soup_peer soup_peer;

    sess.login(username, pass);
    soup_peer.reject_login(sock, tcp_peer);
    sock.poll(); sess.poll();

    CHECK(sess.is_disconnected());
    CHECK(!sess.test_have_session());
    CHECK(!app.enter_order("t", 1, 'B', 1, 1, 0, 0, "a"));

    CHECK(sess.login(username, pass));

    const auto soup = payload_of(cap.back());
    CHECK(soup.size() == 49);
    CHECK(static_cast<char>(soup[2]) == 'L');
    CHECK(std::memcmp(soup.data() + 19, "          ", 10) == 0);
    CHECK(static_cast<char>(soup[48]) == '1');
    CHECK(static_cast<char>(soup[47]) == ' ');

    resource_checks
}

void test_soup_rx_timeout() {
    establish_soup

    io::cycle_timer::elapse(soup::RX_TIMEOUT_MS);
    sock.poll(); sess.poll();

    CHECK(sess.is_disconnected());
    CHECK(sock.is_closed());
    CHECK(tcp_of(cap.back())->control & RST_FLAG);

    resource_checks
}

void test_soup_heartbeat2() {
    establish_soup

    io::cycle_timer::elapse(DELAYED_ACK_TIMEOUT_MILLISECONDS);
    sock.poll();

    const auto before = cap.size();

    io::cycle_timer::elapse(soup::HEARTBEAT_MS / 2);
    sock.poll(); sess.poll();
    CHECK(cap.size() == before);

    std::array<std::byte, 8> junk{};
    CHECK(sess.send_unsequenced(junk));
    tcp_peer.inject(sock, ACK_FLAG);
    sock.poll();

    io::cycle_timer::elapse(soup::HEARTBEAT_MS / 2);
    sock.poll(); sess.poll();

    CHECK(cap.size() == before + 1);

    io::cycle_timer::elapse(soup::HEARTBEAT_MS / 2 + 1);
    sock.poll(); sess.poll();
    CHECK(cap.size() == before + 2);

    const auto heart = payload_of(cap.back());
    CHECK(heart.size() == 3);
    CHECK(heart[2] == static_cast<std::byte>('R'));

    resource_checks
}

void test_soup_enter_order_wire() {
    establish_soup

    CHECK(app.enter_order("tok1", 42, 'B', 100, 1234, 0, 1, "acct"));

    auto pkt = payload_of(cap.back());

    CHECK(pkt.size() == sizeof(ouch::enter_order_msg) + 3);
    CHECK(*reinterpret_cast<u16*>(pkt.data()) == to_net<u16>(sizeof(ouch::enter_order_msg) + 1));
    CHECK(pkt[2] == static_cast<std::byte>('U'));

    const auto* order = reinterpret_cast<ouch::enter_order_msg*>(pkt.data() + 3);

    CHECK(order->msg_type == ouch::ENTER_ORDER_VAL);
    CHECK(std::memcmp(order->order_token, "tok1          ", 14) == 0);
    CHECK(order->order_book_id == to_net<u32>(42));
    CHECK(order->side == 'B');
    CHECK(order->quantity == to_net<u64>(100));
    CHECK(order->price == static_cast<i32>(to_net<u32>(1234)));
    CHECK(order->time_in_force == 0);
    CHECK(order->open_close == 1);
    CHECK(std::memcmp(order->client_account, "acct            ", 16) == 0);

    resource_checks
}
