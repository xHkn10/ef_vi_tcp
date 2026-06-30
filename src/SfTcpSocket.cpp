#include <cstring>
#include <netinet/in.h>

#include <numeric>
#include <ranges>
#include <algorithm>

#include <etherfabric/vi.h>
#include <etherfabric/ef_vi.h>
#include <chrono>

#include "ef_vi_stuff.h"
#include "SfTcpSocket.h"
#include "config.h"
#include "types.h"
#include "net_headers.h"

namespace {
    template<typename T>
    T pop_back(std::vector<T>& vec) {
        T tmp = vec.back();
        vec.pop_back();
        return tmp;
    }
}

SfTcpSocket::SfTcpSocket() {
    if (int rc = ctx.init_ef_vi(); rc < 0)
        throw std::runtime_error("socket couldn't initialized, rc = " + std::to_string(rc));

    tcb = {};
    local_ip = 0;
    local_port = 0;
    remote_ip = 0;
    remote_port = 0;
    is_bound = false;

    for (pkt_buf* pb : ctx.tx_pkt_bufs) {
        write_headers(pb);
        pb->meta = {{pb->dma_buf + TCP_TOTAL_HDR_SZ, TCP_MAX_PAYLOAD_SZ}, nullptr, 0, 2};
    }

    CycleTimer::calibrate();
}

bool SfTcpSocket::bind(u32 local_ip, u16 local_port) {
    ef_filter_spec filter_spec;
    ef_filter_spec_init(&filter_spec, EF_FILTER_FLAG_NONE);

    if (int rc = ef_filter_spec_set_ip4_local(&filter_spec, IPPROTO_TCP, local_ip, local_port) < 0) {
        fprintf(stderr, "ef_filter_spec_set_ip4_local: %s\n", strerror(-rc));
        return false;
    }

    if (int rc = ef_vi_filter_add(&ctx.vi, ctx.dh, &filter_spec, nullptr) < 0) {
        fprintf(stderr, "ef_vi_filter_add: %s\n", strerror(-rc));
        return false;
    }

    this->local_ip = local_ip;
    this->local_port = local_port;

    for (auto& pb : ctx.tx_pkt_bufs) {
        get_ip_hdr(pb)->s_addr = htonl(local_ip);
        get_tcp_hdr(pb)->src_port = htons(local_port);
    }

    is_bound = true;

    return true;
}

// Note: connect spins (it's blocking)
bool SfTcpSocket::connect(u32 remote_ip, u16 remote_port, u8 dmac[6], u8 smac[6]) {
    if (ctx.tx_free_stk.empty())
        return false;

    tcb.state = TcpState::SYN_SENT;
    tcb.ISS = generate_iss();
    tcb.SND_UNA = tcb.ISS;
    tcb.SND_NXT = tcb.ISS + 1;

    this->remote_ip = remote_ip;
    this->remote_port = remote_port;

    for (auto& pb : ctx.tx_pkt_bufs) {
        eth_hdr* eth = get_eth_hdr(pb);
        std::memcpy(eth->dmac, dmac, 6);
        std::memcpy(eth->smac, smac, 6);
        get_ip_hdr(pb)->d_addr = htonl(remote_ip);
        get_tcp_hdr(pb)->dst_port = htons(remote_port);
    }

    {
        int id = pop_back(ctx.tx_free_stk);
        pkt_buf* pb = ctx.tx_pkt_bufs[id];
        get_tcp_hdr(pb)->control |= SYN_FLAG;
        get_tcp_hdr(pb)->control &= ~ACK_FLAG;
        ef_vi_transmit(&ctx.vi, pb->dma_buf_addr, TCP_TOTAL_HDR_SZ, id);
    }

    // spin until poll_once transitions out of SYN_SENT (to ESTABLISHED, ideally)
    const auto deadline = CycleTimer::now() + CycleTimer::cycles_per_ms * CONNECT_TIMEOUT_MILLISECONDS;
    for (int spins = 0; tcb.state == TcpState::SYN_SENT; ++spins) {
        if ((spins & 0xFF) == 0 && CycleTimer::now() > deadline) {
            std::puts("Connection timeout\n");
            return false;
        }
        poll_once();
    }

    return tcb.state == TcpState::ESTABLISHED;
}

bool SfTcpSocket::close() {
    if (tcb.state != TcpState::ESTABLISHED && tcb.state != TcpState::CLOSE_WAIT)
        return false;
    if (ctx.tx_free_stk.empty() || ef_vi_transmit_space(&ctx.vi) == 0)
        return false;

    int id = pop_back(ctx.tx_free_stk);
    pkt_buf* pb = ctx.tx_pkt_bufs[id];

    auto* tcp = get_tcp_hdr(pb);
    tcp->control |= FIN_FLAG | ACK_FLAG;
    tcp->seq_num = htonl(tcb.SND_NXT++);
    tcp->ack_num = htonl(tcb.RCV_NXT);

    tcb.state = tcb.state == TcpState::ESTABLISHED ? TcpState::FIN_WAIT1 : TcpState::LAST_ACK;

    ef_vi_transmit(&ctx.vi, pb->dma_buf_addr, TCP_TOTAL_HDR_SZ, id);
    return true;
}

bool SfTcpSocket::abort() {
    if (tcb.state != TcpState::ESTABLISHED)
        return false;
    if (ctx.tx_free_stk.empty() || ef_vi_transmit_space(&ctx.vi) == 0)
        return false;
    int id = pop_back(ctx.tx_free_stk);
    pkt_buf* pb = ctx.tx_pkt_bufs[id];

    auto* tcp = get_tcp_hdr(pb);

    tcp->control |= RST_FLAG;
    tcp->seq_num = htonl(tcb.SND_NXT);
    tcp->ack_num = htonl(tcb.RCV_NXT);

    ef_vi_transmit(&ctx.vi, pb->dma_buf_addr, TCP_TOTAL_HDR_SZ, id);

    return true;
}

bool SfTcpSocket::send(pkt_buf* pb) {
    if (tcb.state != TcpState::ESTABLISHED || ef_vi_transmit_space(&ctx.vi) == 0)
        return false;
    stamp_and_send<false>(pb, pb->meta.payload.size());
    return true;
}

bool SfTcpSocket::send(tx_sgl&& sgl) {
    if (tcb.state != TcpState::ESTABLISHED)
        return false;
    if (sgl.segments.empty())
        return false;

    // TODO should we partially send the segments?
    if (ef_vi_transmit_space(&ctx.vi) < sgl.segments.size())
        return false;

    for (pkt_buf* seg : sgl.segments)
        stamp_and_send<true>(seg, seg->meta.payload.size());

    ef_vi_transmit_push(&ctx.vi);
    return true;
}

int SfTcpSocket::send(std::span<const std::byte> payload) {
    if (tcb.state != TcpState::ESTABLISHED)
        return 0;

    int n_bytes_sent = 0;

    while (n_bytes_sent < payload.size() && !ctx.tx_free_stk.empty() && ef_vi_transmit_space(&ctx.vi) > 0) {
        int id = pop_back(ctx.tx_free_stk);
        pkt_buf* pb = ctx.tx_pkt_bufs[id];

        int chunk = std::min(TCP_MAX_PAYLOAD_SZ, static_cast<int>(payload.size()) - n_bytes_sent);
        std::memcpy(pb->dma_buf + TCP_TOTAL_HDR_SZ, payload.data() + n_bytes_sent, chunk);
        stamp_and_send<true>(pb, chunk);

        n_bytes_sent += chunk;
    }

    ef_vi_transmit_push(&ctx.vi);

    return n_bytes_sent;
}

void SfTcpSocket::write_headers(pkt_buf* pb) const {
    eth_hdr eh{}; // TODO fill in smac dmac

    // NO ip options, NO ip fragmentation
    ip_hdr ih{
        0x45,
        0, // No dscp, no ecn
        0,
        0,
        htons(0x4000),
        255,
        IPPROTO_TCP,
        0,
        htonl(local_ip),
        htonl(remote_ip)
    };

    tcp_hdr th{
        htons(local_port),
        htons(remote_port),
        htonl(tcb.ISS),
        0,
        0x50,
        0, // CWR, ECE, URG, ACK, PSH, RST, SYN, FIN; since this is a varying field, it should be always set explicitly
        htons(0xFFFF),
        0,
        0
    };

    *get_eth_hdr(pb) = eh;
    *get_ip_hdr(pb) = ih;
    *get_tcp_hdr(pb) = th;
}


int SfTcpSocket::receive(std::span<std::byte> spn) {
    poll_once();

    rx_sgl sgl = tcb.hand_out_ready();
    pkt_buf* cur_rx = sgl.head;

    int n_bytes_left = spn.size();
    while (cur_rx != nullptr && n_bytes_left > 0) {
        if (cur_rx->meta.payload.size() <= n_bytes_left) {
            std::memcpy(spn.last(n_bytes_left).data(), cur_rx->meta.payload.data(), cur_rx->meta.payload.size());
            n_bytes_left -= cur_rx->meta.payload.size();
            ctx.rx_free_stk.push_back(cur_rx->id);
        } else {
            std::memcpy(spn.last(n_bytes_left).data(), cur_rx->meta.payload.data(), n_bytes_left);
            cur_rx->meta.payload = cur_rx->meta.payload.subspan(n_bytes_left);
            n_bytes_left = 0;
        }
        cur_rx = cur_rx->meta.nxt;
    }

    int n_bytes_read = static_cast<int>(spn.size()) - n_bytes_left;

    if (cur_rx) {
        tcb.rx_ready_head = cur_rx;
        tcb.ready_bytes -= n_bytes_read;
    } else {
        tcb.rx_ready_head = tcb.rx_ready_tail = nullptr;
        tcb.ready_bytes = 0;
    }

    return n_bytes_read;
}

pkt_buf* SfTcpSocket::receive_single() {
    poll_once();
    return tcb.pop_front();
}

rx_sgl SfTcpSocket::receive_available() {
    poll_once();
    return tcb.hand_out_ready();
}

int SfTcpSocket::consume(rx_sgl sgl, int bytes_to_consume) {
    int bytes_left = bytes_to_consume;
    pkt_buf* cur_rx = sgl.head;

    while (cur_rx != nullptr && bytes_left > 0) {
        if (bytes_left >= cur_rx->meta.payload.size()) {
            bytes_left -= cur_rx->meta.payload.size();
            ctx.rx_free_stk.push_back(cur_rx->id);
            cur_rx = cur_rx->meta.nxt;
        } else {
            cur_rx->meta.payload = cur_rx->meta.payload.subspan(bytes_left);
            cur_rx->meta.seq += bytes_left; // TODO IS THIS NEEDED?
            bytes_left = 0;
        }
    }

    if (cur_rx) {
        tcb.rx_ready_head = cur_rx;
        tcb.rx_ready_tail = sgl.tail;
        int actual_consumed = bytes_to_consume - bytes_left;
        tcb.ready_bytes = sgl.n_bytes - actual_consumed;
    } else {
        tcb.rx_ready_head = tcb.rx_ready_tail = nullptr;
        tcb.ready_bytes = 0;
    }

    refill_rx_ring();
    
    return bytes_to_consume - bytes_left;
}

template <bool queue>
void SfTcpSocket::stamp_and_send(pkt_buf* pb, int payload_sz) {
    get_ip_hdr(pb)->len = htons(IP_HDR_SZ + TCP_HDR_SZ + payload_sz);

    tcp_hdr* tcp = get_tcp_hdr(pb);
    tcp->seq_num = htonl(tcb.SND_NXT);
    tcp->ack_num = htonl(tcb.RCV_NXT);
    tcp->control |= ACK_FLAG;
    tcb.SND_NXT += payload_sz;
    tcb.need_ack = tcb.immediate_ack_req = false;

    pb->meta.tx_ref_cnt = 2;
    tcb.tx_unacked.push_back(pb);
    if (tcb.tx_unacked.size() == 1)
        tcb.rto_deadline_cycles = CycleTimer::now() + CycleTimer::cycles_per_ms * RTO_MILLISECONDS;

    if constexpr (queue)
        ef_vi_transmit_init(&ctx.vi, pb->dma_buf_addr, TCP_TOTAL_HDR_SZ + payload_sz, pb->id);
    else
        ef_vi_transmit(&ctx.vi, pb->dma_buf_addr, TCP_TOTAL_HDR_SZ + payload_sz, pb->id);
}


pkt_buf* SfTcpSocket::get_tx_buf() {
    if (ctx.tx_free_stk.empty())
        return nullptr;

    int id = pop_back(ctx.tx_free_stk);
    pkt_buf* pb = ctx.tx_pkt_bufs[id];
    return pb;
}

tx_sgl SfTcpSocket::get_tx_sgl(int n_bytes) {
    tx_sgl sgl{};

    for (int left = n_bytes; left > 0 && !ctx.tx_free_stk.empty(); left -= TCP_MAX_PAYLOAD_SZ) {
        int id = pop_back(ctx.tx_free_stk);
        pkt_buf* pb = ctx.tx_pkt_bufs[id];
        sgl.segments.push_back(pb);
        sgl.n_bytes += TCP_MAX_PAYLOAD_SZ;
    }

    return sgl;
}

u32 SfTcpSocket::generate_iss() {
    return 0;
}

void SfTcpSocket::refill_rx_ring() {
    int cnt = std::min<int>(ef_vi_receive_space(&ctx.vi), ctx.rx_free_stk.size());
    cnt &= ~(REFILL_BATCH_SZ - 1);
    if (cnt == 0)
        return;
    for (int i = 0; i < cnt; ++i) {
        int id = pop_back(ctx.rx_free_stk);
        ef_vi_receive_init(&ctx.vi, ctx.rx_pkt_bufs[id]->dma_buf_addr, id);
    }
    ef_vi_receive_push(&ctx.vi);
}

void SfTcpSocket::send_pure_ack() {
    // best effort
    if (ctx.tx_free_stk.empty() || ef_vi_transmit_space(&ctx.vi) == 0)
        return;
    int id = pop_back(ctx.tx_free_stk);
    pkt_buf* pb = ctx.tx_pkt_bufs[id];

    get_ip_hdr(pb)->len = htons(IP_HDR_SZ + TCP_HDR_SZ);

    auto* tcp = get_tcp_hdr(pb);
    tcp->control = ACK_FLAG;
    tcp->seq_num = htonl(tcb.SND_NXT);
    tcp->ack_num = htonl(tcb.RCV_NXT);

    tcb.need_ack = tcb.immediate_ack_req = false;
    pb->meta.tx_ref_cnt = 1;

    ef_vi_transmit(&ctx.vi, pb->dma_buf_addr, TCP_TOTAL_HDR_SZ, id);
}

void SfTcpSocket::poll_once() {
    ef_event events[POLL_BATCH_SZ];
    int n_events = ef_eventq_poll(&ctx.vi, events, POLL_BATCH_SZ);
    for (auto& event : events | std::views::take(n_events)) {
        switch (EF_EVENT_TYPE(event)) {
            case EF_EVENT_TYPE_RX: {
                int id = EF_EVENT_RX_RQ_ID(event);
                pkt_buf* pb = ctx.rx_pkt_bufs[id];
                tcp_hdr* tcp = get_tcp_hdr(pb);

                // RST
                if (tcp->control & RST_FLAG) {
                    tcb.state = TcpState::CLOSED;
                    ctx.rx_free_stk.push_back(id);
                    break;
                }

                // 3 WHS
                if (tcb.state == TcpState::SYN_SENT) {
                    if ((tcp->control & (SYN_FLAG | ACK_FLAG)) == (SYN_FLAG | ACK_FLAG) && ntohl(tcp->ack_num) == tcb.ISS + 1) {
                        tcb.RCV_NXT = ntohl(tcp->seq_num) + 1;
                        tcb.SND_UNA = ntohl(tcp->ack_num);
                        tcb.state = TcpState::ESTABLISHED;
                        send_pure_ack();
                    }
                    // TODO simultaneous open
                    ctx.rx_free_stk.push_back(id);
                    break;
                }

                // ACK
                if (tcp->control & ACK_FLAG)
                    tcb.handle_ack(ntohl(tcp->ack_num), ctx.tx_free_stk);
                else
                    std::puts("Received segment without ACK\n");

                auto payload = get_tcp_payload(pb);
                if (payload.empty() && !(tcp->control & FIN_FLAG))
                    ctx.rx_free_stk.push_back(id);
                else {
                    pb->meta = {payload, nullptr, ntohl(tcp->seq_num)};
                    tcb.rx_out_of_order.insert(pb);
                }
                break;
            }
            case EF_EVENT_TYPE_TX_ERROR:
            case EF_EVENT_TYPE_TX: {
                ef_request_id ids[EF_VI_TRANSMIT_BATCH];
                int n_ids = ef_vi_transmit_unbundle(&ctx.vi, &event, ids);
                for (auto id : ids | std::views::take(n_ids)) {
                    pkt_buf* pb = ctx.tx_pkt_bufs[id];
                    if (--pb->meta.tx_ref_cnt == 0)
                        ctx.tx_free_stk.push_back(id);
                }
                break;
            }
            default: {
                std::puts("Unknown event\n");
                break;
            }
        }
    }

    tcb.process(ctx.rx_free_stk);

    if (tcb.immediate_ack_req || (tcb.need_ack && CycleTimer::now() >= tcb.ack_deadline_cycles))
        send_pure_ack();

    if (!tcb.tx_unacked.empty() && CycleTimer::now() > tcb.rto_deadline_cycles)
        retransmit_head();

    refill_rx_ring();
}

void SfTcpSocket::retransmit_head() {
    // retry in next poll_once
    if (ef_vi_transmit_space(&ctx.vi) == 0)
        return;

    pkt_buf* pb = tcb.tx_unacked.peek_front();
    tcp_hdr* tcp = get_tcp_hdr(pb);
    tcp->ack_num = htonl(tcb.RCV_NXT);
    ++(pb->meta.tx_ref_cnt);

    ef_vi_transmit(&ctx.vi, pb->dma_buf_addr, TCP_TOTAL_HDR_SZ + pb->meta.payload.size(), pb->id);

    tcb.rto_deadline_cycles = CycleTimer::now() + CycleTimer::cycles_per_ms * RTO_MILLISECONDS;
}

SfTcpSocket::~SfTcpSocket() {
    if (tcb.state != TcpState::CLOSED)
        abort();
    tcb.state = TcpState::CLOSED;
}
