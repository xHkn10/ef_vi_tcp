#include "test_utils.h"
#include "account/Account.h"
#include "engine/Engine.h"

void test_engine_lifecycle() {
    io::test::test_reset();
    Account acc{};
    acc.cfg.username = username;
    acc.cfg.pass = pass;
    fake_tcp_peer tcp_peer;
    fake_soup_peer soup_peer;
    auto& cap = io::test::g_sent_captured;
    CHECK(acc.sock.bind(LOCAL_IP, LOCAL_PORT, REMOTE_IP, REMOTE_PORT, dummy_mac, dummy_mac));

    engine::Engine<1>::step(acc);
    CHECK(acc.phase == Phase::Connecting);
    CHECK(tcp_of(cap.back())->control == SYN_FLAG);

    tcp_peer.inject(acc.sock, SYN_FLAG | ACK_FLAG);
    acc.sock.poll();
    engine::Engine<1>::step(acc);
    CHECK(acc.phase == Phase::LoggingIn);
    CHECK(static_cast<char>(payload_of(cap.back())[2]) == 'L');

    soup_peer.accept_login(acc.sock, tcp_peer);
    acc.sock.poll();
    engine::Engine<1>::step(acc);
    CHECK(acc.phase == Phase::Active);
}

