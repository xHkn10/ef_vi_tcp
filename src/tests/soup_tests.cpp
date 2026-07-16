#include "test_utils.h"
#include "soup/Session.h"

std::string_view username = "hakan";
std::string_view pass = "pass";

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

    int init_cap_sz = cap.size();

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
