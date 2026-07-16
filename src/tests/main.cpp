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

void test_soup_login();
void test_soup_login_rej();
void test_soup_logout();
void test_soup_seq_num();
void test_soup_resume_login();

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
};

static auto run_soup_tests = [] {
    test_soup_login();
    test_soup_login_rej();
    test_soup_logout();
    test_soup_seq_num();
    test_soup_resume_login();
};

static auto run_engine_tests = [] {

};

int main() {
    run_tcp_tests();
    run_soup_tests();
    run_engine_tests();

    printf("%d errors\n", g_failures);
}
