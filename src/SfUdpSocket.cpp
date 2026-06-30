#include <cstdio>
#include <cstring>
#include <SfUdpSocket.h>

#include <etherfabric/ef_vi.h>
#include <etherfabric/vi.h>
#include <netinet/in.h>

#include "net_headers.h"
#include "TxBuffer.h"

SfUdpSocket::SfUdpSocket() {
    if (int rc = ef_ctx.init_ef_vi(); rc < 0)
        throw std::runtime_error("socket couldn't initialized, rc = " + std::to_string(rc));
    local_ip = 0;
    local_port = 0;
    is_bound = false;
    internal_rx_stk.reserve(N_RX_BUFS);
}

SfUdpSocket::~SfUdpSocket() {
    ef_ctx.teardown();
}

std::optional<TxBuffer> SfUdpSocket::get_tx_buf() {
    if (ef_ctx.tx_free_stk.empty())
        return std::nullopt;

    const int id = ef_ctx.tx_free_stk.back();
    ef_ctx.tx_free_stk.pop_back();

    pkt_buf* pb = ef_ctx.tx_pkt_bufs[id];
    return TxBuffer{{pb->dma_buf + 32, BUF_SZ - 32 - 64}, id};
}

std::optional<RxBuffer> SfUdpSocket::receive(u32 &src_ip, u16 &src_port) {
    ef_event events[POLL_BATCH_SZ];
    ef_request_id ids[EF_VI_TRANSMIT_BATCH];

    const int n_events = ef_eventq_poll(&ef_ctx.vi, events, POLL_BATCH_SZ);

    for (int i = 0; i < n_events; ++i) {
        auto& event = events[i];
        switch (EF_EVENT_TYPE(event)) {
            case EF_EVENT_TYPE_RX: {
                internal_rx_stk.push_back(EF_EVENT_RX_RQ_ID(event));
                break;
            }
            case EF_EVENT_TYPE_TX:
            case EF_EVENT_TYPE_TX_ERROR: {
                const int n_completed = ef_vi_transmit_unbundle(&ef_ctx.vi, &event, ids);
                for (int j = 0; j < n_completed; ++j)
                    ef_ctx.tx_free_stk.push_back(ids[j]);
                break;
            }
            default: {
                std::puts("Unknown event type\n");
                break;
            }
        }
    }

    if (!internal_rx_stk.empty()) {
        const int id = internal_rx_stk.back();
        internal_rx_stk.pop_back();
        pkt_buf* pb = ef_ctx.rx_pkt_bufs[id];
        get_ip_port_from_received_buf(pb, src_ip, src_port);
        u32 udp_payload_len = get_udp_len_field(pb) - UDP_HDR_SZ;
        return RxBuffer{{pb->dma_buf + UDP_TOTAL_HDR_SZ, udp_payload_len}, &ef_ctx, id};
    }

    return std::nullopt;
}

void SfUdpSocket::get_ip_port_from_received_buf(const pkt_buf *pb, u32 &src_ip, u16 &src_port) {
    std::memcpy(&src_ip, pb->dma_buf + IP_S_ADDR_OFFSET, 4);
    std::memcpy(&src_port, pb->dma_buf + UDP_S_PORT_OFFSET, 2);
}

u32 SfUdpSocket::get_udp_len_field(const pkt_buf* pb) {
    u32 len;
    std::memcpy(&len, pb->dma_buf + UDP_LEN_OFFSET, sizeof(len));
    return len;
}

bool SfUdpSocket::send(TxBuffer&& buf, u32 dst_ip, u16 dst_port) {
    const int id = buf.id;
    pkt_buf* pb = ef_ctx.tx_pkt_bufs[id];

    std::memcpy(pb->dma_buf + IP_D_ADDR_OFFSET, &dst_ip, sizeof(dst_ip));
    std::memcpy(pb->dma_buf + UDP_D_PORT_OFFSET, &dst_port, sizeof(dst_port));

    ef_vi_transmit(&ef_ctx.vi, pb->dma_buf_addr, buf.buf.size() + UDP_TOTAL_HDR_SZ, id);

    ef_ctx.tx_free_stk.push_back(id);

    return true;
}

bool SfUdpSocket::bind(u32 local_ip, u16 local_port) {
    this->local_ip = local_ip;
    this->local_port = local_port;

    ef_filter_spec filter_spec;
    ef_filter_spec_init(&filter_spec, EF_FILTER_FLAG_NONE);

    if (int rc = ef_filter_spec_set_ip4_local(&filter_spec, IPPROTO_UDP, local_ip, local_port) < 0) {
        fprintf(stderr, "ef_filter_spec_set_ip4_local: %s\n", strerror(-rc));
        return false;
    }

    if (int rc = ef_vi_filter_add(&ef_ctx.vi, ef_ctx.dh, &filter_spec, nullptr) < 0) {
        fprintf(stderr, "ef_vi_filter_add: %s\n", strerror(-rc));
        return false;
    }

    return true;
}
