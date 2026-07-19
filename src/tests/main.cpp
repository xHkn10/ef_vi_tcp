#include <cstdio>
#include "test_utils.h"

void test_handshake();
void test_rst_during_handshake();
void test_passive_close();
void test_active_close1();
void test_active_close2();
void test_active_abort();
void test_inplace_send();
void test_big_span_send();
void test_tx_sgl_send();
void test_receive();
void test_ooo_receive();
void test_rto();
void test_duplicate_send();
void test_erroneous_handshake();
void test_wraparound();
void test_partial_ack_received();
void test_huge_send_recv_simultaneously();
void test_tcp_time_wait_transition();
void rx_ooo_insert_test();

void test_soup_login();
void test_soup_login_rej();
void test_soup_logout();
void test_soup_seq_num();
void test_soup_resume_login();
void test_soup_fragmentation();
void test_soup_heartbeat();
void test_soup_end_of_session();
void test_soup_multiple_sequenced_in_one_segment();
void test_soup_unknown_type_skip();
void test_soup_zero_length_fatal();
void test_soup_relogin_after_reject();
void test_soup_rx_timeout();
void test_soup_heartbeat2();
void test_soup_enter_order_wire();

void test_engine_login();
void test_engine_reconnect1();
void test_engine_reconnect2();
void test_engine_login_rejected();
void test_engine_resume();
void test_engine_rx_timeout();
void test_engine_logout();

static auto run_tcp_tests = [] {
    test_handshake();
    test_rst_during_handshake();
    test_passive_close();
    test_active_close1();
    test_active_close2();
    test_active_abort();
    test_inplace_send();
    test_big_span_send();
    test_tx_sgl_send();
    test_receive();
    test_ooo_receive();
    test_rto();
    test_duplicate_send();
    test_erroneous_handshake();
    test_wraparound();
    test_partial_ack_received();
    test_huge_send_recv_simultaneously();
    test_tcp_time_wait_transition();
    rx_ooo_insert_test();
};

static auto run_soup_tests = [] {
    test_soup_login();
    test_soup_login_rej();
    test_soup_logout();
    test_soup_seq_num();
    test_soup_resume_login();
    test_soup_fragmentation();
    test_soup_heartbeat();
    test_soup_end_of_session();
    test_soup_multiple_sequenced_in_one_segment();
    test_soup_unknown_type_skip();
    test_soup_zero_length_fatal();
    test_soup_relogin_after_reject();
    test_soup_rx_timeout();
    test_soup_heartbeat2();
    test_soup_enter_order_wire();
};

static auto run_engine_tests = [] {
    test_engine_login();
    test_engine_reconnect1();
    test_engine_reconnect2();
    test_engine_login_rejected();
    test_engine_resume();
    test_engine_rx_timeout();
    test_engine_logout();
};

int main() {
    run_tcp_tests();
    run_soup_tests();
    run_engine_tests();

    std::printf("%d errors\n", g_failures);
}
