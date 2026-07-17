#include "test_utils.h"
#include "account/Account.h"
#include "engine/Engine.h"

void test_engine_login() {
    establish_engine
    engine_resource_checks
}

void test_engine_reconnect1() {
    engine_setup

    engine::Engine<1>::step(acc);
    CHECK(acc.phase == Phase::Connecting);
    CHECK(tcp_of(cap.back())->control == SYN_FLAG);

    tcp_peer.inject(acc.sock, RST_FLAG);
    acc.sock.poll();
    CHECK(acc.sock.is_closed());

    engine::Engine<1>::step(acc);
    CHECK(acc.phase == Phase::Idle);

    engine_connect
    engine_accept_login
    engine_end_session
    engine_resource_checks
}

void test_engine_reconnect2() {
    engine_setup
    engine_connect

    tcp_peer.inject(acc.sock, RST_FLAG);
    acc.sock.poll();
    CHECK(acc.sock.is_closed());

    engine::Engine<1>::step(acc);
    CHECK(acc.phase == Phase::Idle);

    engine_connect
    engine_accept_login
    engine_end_session

    engine_resource_checks
}

void test_engine_login_rejected() {
    engine_setup
    engine_connect

    soup_peer.reject_login(acc.sock, tcp_peer);
    acc.sock.poll();
    engine::Engine<1>::step(acc);

    CHECK(acc.phase == Phase::Idle);
    CHECK(acc.sock.is_closed());
    CHECK(tcp_of(cap.back())->control & RST_FLAG);

    engine_connect

    const auto soup = payload_of(cap.back());
    CHECK(soup.size() == 49);
    CHECK(std::memcmp(soup.data() + 19, "          ", 10) == 0);
    CHECK(static_cast<char>(soup[48]) == '1');

    engine_accept_login
    engine_resource_checks
}

void test_engine_resume() {
    establish_engine

    soup_peer.send_sequenced(acc.sock, tcp_peer);
    acc.sock.poll();
    engine::Engine<1>::step(acc);
    CHECK(acc.session.test_seq_num() == 2);

    // connection dies mid-session
    tcp_peer.inject(acc.sock, RST_FLAG);
    acc.sock.poll();
    CHECK(acc.sock.is_closed());

    engine::Engine<1>::step(acc);
    CHECK(acc.phase == Phase::Idle);
    CHECK(acc.session.test_have_session()); // resume state survives teardown

    engine_connect

    // the relogin must ask for the old session at seq 2
    const auto soup = payload_of(cap.back());
    CHECK(soup.size() == 49);
    CHECK(std::memcmp(soup.data() + 19, "    dümen", 10) == 0);
    CHECK(static_cast<char>(soup[48]) == '2');
    CHECK(static_cast<char>(soup[47]) == ' ');

    soup_peer.accept_login(acc.sock, tcp_peer, 2);
    acc.sock.poll();
    engine::Engine<1>::step(acc);
    CHECK(acc.phase == Phase::Active);
    CHECK(acc.session.test_seq_num() == 2);

    soup_peer.send_sequenced(acc.sock, tcp_peer);
    acc.sock.poll();
    engine::Engine<1>::step(acc);
    CHECK(acc.session.test_seq_num() == 3);

    engine_resource_checks
}

void test_engine_rx_timeout() {
    establish_engine

    io::cycle_timer::elapse(soup::RX_TIMEOUT_MS);
    acc.sock.poll();
    engine::Engine<1>::step(acc); // check_timers aborts -> engine tears down

    CHECK(acc.phase == Phase::Idle);
    CHECK(acc.sock.is_closed());
    CHECK(acc.session.test_have_session()); // timeout is resumable, not terminal

    engine::Engine<1>::step(acc); // engine reconnects on its own
    CHECK(acc.phase == Phase::Connecting);
    CHECK(tcp_of(cap.back())->control == SYN_FLAG);

    engine_resource_checks
}

void test_engine_logout() {
    establish_engine

    CHECK(acc.session.logout());
    engine::Engine<1>::step(acc);
    CHECK(acc.phase == Phase::Stopped);
    CHECK(!acc.session.test_have_session());
    CHECK(tcp_of(cap.back())->control == (FIN_FLAG | ACK_FLAG));

    const auto cap_sz = cap.size();
    for (int i = 0; i < 100; ++i)
        engine::Engine<1>::step(acc);
    CHECK(acc.phase == Phase::Stopped);
    CHECK(cap.size() == cap_sz);

    tcp_peer.inject(acc.sock, ACK_FLAG);
    acc.sock.poll();
    tcp_peer.inject(acc.sock, FIN_FLAG | ACK_FLAG);
    acc.sock.poll();
    CHECK(!acc.sock.is_established());

    engine::Engine<1>::step(acc);
    CHECK(acc.phase == Phase::Stopped);

    engine_resource_checks
}
