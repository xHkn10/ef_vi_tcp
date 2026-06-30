#pragma once

#include <optional>

#include "ef_vi_stuff.h"
#include "types.h"
#include "RxBuffer.h"
#include "TxBuffer.h"

class SfUdpSocket {
public:
    SfUdpSocket();
    ~SfUdpSocket();

    bool send(TxBuffer&& buf, u32 dst_ip, u16 dst_port);
    std::optional<RxBuffer> receive(u32& src_ip, u16& src_port);

    [[nodiscard]] std::optional<TxBuffer> get_tx_buf();

    bool bind(u32 local_ip, u16 local_port);

private:
    static void get_ip_port_from_received_buf(const pkt_buf* pb, u32& src_ip, u16& src_port);
    static u32 get_udp_len_field(const pkt_buf* pb);

    ef_struct ef_ctx;

    u32 local_ip;
    u16 local_port;
    bool is_bound;

    std::vector<int> internal_rx_stk;
};
