#pragma once

#include "ef_vi_stuff.h"
#include "tcb.h"
#include "types.h"
#include "sgl.h"

// template<bool offload_ip_cs = true, bool offload_tcp_cs = true>
class SfTcpSocket {
public:
    SfTcpSocket();
    ~SfTcpSocket();

    bool bind(u32 local_ip, u16 local_port);
    bool connect(u32 remote_ip, u16 remote_port, u8 dmac[6], u8 smac[6]);

    bool close();
    bool abort();

    pkt_buf* receive_single();
    rx_sgl receive_available();
    int consume(rx_sgl, int bytes_to_consume);
    int receive(std::span<std::byte>);

    bool send(pkt_buf*);
    bool send(tx_sgl&&);
    int send(std::span<const std::byte> payload);

    pkt_buf* get_tx_buf();
    tx_sgl get_tx_sgl(int n_bytes);

private:
    void refill_rx_ring();

    static u32 generate_iss();
    void write_headers(pkt_buf*) const;

    template <bool queue>
    void stamp_and_send(pkt_buf*, int payload_sz);

    void send_pure_ack();

    void poll_once();

    void retransmit_head();

    ef_struct ctx;

    TCB tcb;

    u32 local_ip;
    u16 local_port;
    u32 remote_ip;
    u16 remote_port;

    bool is_bound;
};
