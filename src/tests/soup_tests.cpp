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

    sock.poll();
    sess.poll();

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
        sock.poll();
        sess.poll();
    }

    resource_checks
}

void test_soup_resume_login() {
    establish_soup

    soup_peer.send_sequenced(sock, tcp_peer);
    sock.poll();
    sess.poll();

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

