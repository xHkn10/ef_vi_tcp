#include <cstring>
#include <netinet/in.h>

#include <numeric>
#include <ranges>
#include <algorithm>

#include <etherfabric/vi.h>
#include <etherfabric/ef_vi.h>
#include <chrono>

#include "io/context.h"
#include "tcp/socket.h"
#include "io/config.h"
#include "types.h"
#include "net/net_headers.h"

namespace {
    template<typename T>
    T pop_back(std::vector<T>& vec) {
        T tmp = vec.back();
        vec.pop_back();
        return tmp;
    }
}

namespace tcp {
    socket::socket() {
        if (int rc = ctx.init(); rc < 0)
            throw std::runtime_error("socket couldn't initialized, rc = " + std::to_string(rc));

        tcb = {};
        local_ip = 0;
        local_port = 0;
        remote_ip = 0;
        remote_port = 0;
        is_bound = false;

        for (io::pkt_buf* pb : ctx.tx_pkt_bufs) {
            write_headers(pb);
            pb->meta = {{pb->dma_buf + TCP_TOTAL_HDR_SZ, 0}, nullptr, 0, 2};
        }
    }

    bool socket::bind(u32 local_ip, u16 local_port, u32 remote_ip, u16 remote_port, u8 dmac[6], u8 smac[6]) {
        if (int rc = ctx.add_ip4_tcp_filter(local_ip, local_port); rc < 0) {
            LOG_ERROR("add_ip4_tcp_filter: %s", strerror(-rc));
            return false;
        }

        this->local_ip = local_ip;
        this->local_port = local_port;

        for (auto& pb : ctx.tx_pkt_bufs) {
            net::get_ip_hdr(pb)->s_addr = to_net(local_ip);
            net::get_tcp_hdr(pb)->src_port = to_net(local_port);
        }

        this->remote_ip = remote_ip;
        this->remote_port = remote_port;

        for (auto& pb : ctx.tx_pkt_bufs) {
            net::eth_hdr* eth = net::get_eth_hdr(pb);
            std::memcpy(eth->dmac, dmac, 6);
            std::memcpy(eth->smac, smac, 6);
            net::get_ip_hdr(pb)->d_addr = to_net(remote_ip);
            net::get_tcp_hdr(pb)->dst_port = to_net(remote_port);
        }

        is_bound = true;

        return true;
    }

    // Connect is non-blocking, as it should be, since in case we lose the connection,
    // we should be able to reconnect in a non-blocking way
    bool socket::connect() {
        if (ctx.tx_free_stk.empty() || tcb.state != fsm::CLOSED || !is_bound) [[unlikely]]
            return false;

        tcb.state = fsm::SYN_SENT;
        tcb.ISS = generate_iss();
        tcb.SND_UNA = tcb.ISS;
        tcb.SND_NXT = tcb.ISS;

        const int id = pop_back(ctx.tx_free_stk);
        io::pkt_buf* pb = ctx.tx_pkt_bufs[id];

        pb->set_payload_sz(0);

        net::get_tcp_hdr(pb)->control = SYN_FLAG;
        net::get_ip_hdr(pb)->len = to_net<u16>(TCP_HDR_SZ + IP_HDR_SZ);

        stamp_and_send<false, false>(pb);

        return true;
    }

    bool socket::close() {
        if (tcb.state != fsm::ESTABLISHED && tcb.state != fsm::CLOSE_WAIT)
            return false;
        if (ctx.tx_free_stk.empty() || ctx.transmit_space() == 0)
            return false;

        const int id = pop_back(ctx.tx_free_stk);
        io::pkt_buf* pb = ctx.tx_pkt_bufs[id];
        pb->set_payload_sz(0);

        net::get_tcp_hdr(pb)->control = FIN_FLAG | ACK_FLAG;

        tcb.state = (tcb.state == fsm::ESTABLISHED) ? fsm::FIN_WAIT1 : fsm::LAST_ACK;

        stamp_and_send<false, false>(pb);

        return true;
    }

    bool socket::abort() {
        if (tcb.state == fsm::CLOSED)
            return false;

        if (!ctx.tx_free_stk.empty() && ctx.transmit_space() > 0) {
            int id = pop_back(ctx.tx_free_stk);
            io::pkt_buf* pb = ctx.tx_pkt_bufs[id];

            pb->meta.tx_ref_cnt = 1;

            auto* tcp = net::get_tcp_hdr(pb);

            tcp->control = RST_FLAG;
            tcp->seq_num = to_net(tcb.SND_NXT);
            tcp->ack_num = to_net(tcb.RCV_NXT);

            net::get_ip_hdr(pb)->len = to_net<u16>(IP_HDR_SZ + TCP_HDR_SZ);

            ctx.transmit(pb->dma_buf_addr, TCP_TOTAL_HDR_SZ, id);
        }

        reset_tcb();
        return true;
    }

    bool socket::send(io::pkt_buf* pb) {
        if (tcb.state != fsm::ESTABLISHED || ctx.transmit_space() == 0)
            return false;
        stamp_and_send<false, true>(pb);
        return true;
    }

    bool socket::send(io::tx_sgl&& sgl) {
        if (tcb.state != fsm::ESTABLISHED)
            return false;
        if (sgl.segments.empty())
            return false;

        // TODO should we partially send the segments?
        if (ctx.transmit_space() < sgl.segments.size())
            return false;

        for (io::pkt_buf* seg : sgl.segments)
            stamp_and_send<true, true>(seg);

        ctx.transmit_push();
        return true;
    }

    int socket::send(std::span<const std::byte> payload) {
        if (tcb.state != fsm::ESTABLISHED || payload.empty()) [[unlikely]]
            return 0;

        int n_bytes_sent = 0;

        while (n_bytes_sent < payload.size() && !ctx.tx_free_stk.empty() && ctx.transmit_space() > 0) {
            const int id = pop_back(ctx.tx_free_stk);
            io::pkt_buf* pb = ctx.tx_pkt_bufs[id];

            const int chunk = std::min(TCP_MAX_PAYLOAD_SZ, static_cast<int>(payload.size()) - n_bytes_sent);
            std::memcpy(pb->dma_buf + TCP_TOTAL_HDR_SZ, payload.data() + n_bytes_sent, chunk);
            pb->set_payload_sz(chunk);

            stamp_and_send<true, true>(pb);

            n_bytes_sent += chunk;
        }

        ctx.transmit_push();

        return n_bytes_sent;
    }

    void socket::write_headers(io::pkt_buf* pb) const {
        net::eth_hdr eh{.ethertype = to_net<u16>(0x0800)}; // smac and dmac filled in bind

        // NO ip options, NO ip fragmentation
        net::ip_hdr ih{
            0x45,
            0, // No dscp, no ecn
            0,
            0,
            to_net<u16>(0x4000),
            255,
            IPPROTO_TCP,
            0,
            to_net(local_ip),
            to_net(remote_ip)
        };

        net::tcp_hdr th{
            to_net(local_port),
            to_net(remote_port),
            to_net(tcb.ISS),
            0,
            0x50,
            0, // CWR, ECE, URG, ACK, PSH, RST, SYN, FIN; since this is a varying field, it should be always set explicitly
            to_net<u16>(0xFFFF),
            0,
            0
        };

        *net::get_eth_hdr(pb) = eh;
        *net::get_ip_hdr(pb) = ih;
        *net::get_tcp_hdr(pb) = th;
    }

    int socket::receive(std::span<std::byte> spn) {
        auto [head, tail, n_bytes] = tcb.hand_out_ready();
        io::pkt_buf* cur_rx = head;

        int n_bytes_left = spn.size();
        while (cur_rx != nullptr && n_bytes_left > 0) {
            if (cur_rx->meta.payload.size() <= n_bytes_left) {
                std::memcpy(spn.last(n_bytes_left).data(), cur_rx->meta.payload.data(), cur_rx->meta.payload.size());
                n_bytes_left -= cur_rx->meta.payload.size();
                ctx.rx_free_stk.push_back(cur_rx->id);
                cur_rx = cur_rx->meta.nxt;
            } else {
                std::memcpy(spn.last(n_bytes_left).data(), cur_rx->meta.payload.data(), n_bytes_left);
                cur_rx->meta.payload = cur_rx->meta.payload.subspan(n_bytes_left);
                n_bytes_left = 0;
            }
        }

        const int n_bytes_read = static_cast<int>(spn.size()) - n_bytes_left;

        if (cur_rx) {
            tcb.rx_ready_head = cur_rx, tcb.rx_ready_tail = tail;
            tcb.ready_bytes = n_bytes - n_bytes_read;
        } else
            tcb.rx_ready_head = tcb.rx_ready_tail = nullptr;

        return n_bytes_read;
    }

    io::pkt_buf* socket::receive_single() {
        return tcb.pop_front();
    }

    io::rx_sgl socket::receive_available() {
        return tcb.hand_out_ready();
    }

    int socket::consume(const io::rx_sgl& sgl, const int bytes_to_consume) {
        int bytes_left = bytes_to_consume;
        io::pkt_buf* cur_rx = sgl.head;

        while (cur_rx != nullptr && bytes_left > 0) {
            if (bytes_left >= cur_rx->meta.payload.size()) {
                bytes_left -= cur_rx->meta.payload.size();
                ctx.rx_free_stk.push_back(cur_rx->id);
                cur_rx = cur_rx->meta.nxt;
            } else {
                cur_rx->meta.payload = cur_rx->meta.payload.subspan(bytes_left);
                bytes_left = 0;
            }
        }

        if (cur_rx) {
            tcb.rx_ready_head = cur_rx, tcb.rx_ready_tail = sgl.tail;
            const int actual_consumed = bytes_to_consume - bytes_left;
            tcb.ready_bytes = sgl.n_bytes - actual_consumed;
        } else {
            tcb.rx_ready_head = tcb.rx_ready_tail = nullptr;
            tcb.ready_bytes = 0;
        }

        refill_rx_ring();

        return bytes_to_consume - bytes_left;
    }

    template <bool defer_doorbell, bool stamp_ack_only>
    void socket::stamp_and_send(io::pkt_buf* pb) {
        const int payload_sz = pb->meta.payload.size();

        net::get_ip_hdr(pb)->len = to_net<u16>(IP_HDR_SZ + TCP_HDR_SZ + payload_sz);

        net::tcp_hdr* tcp = net::get_tcp_hdr(pb);

        if constexpr (stamp_ack_only) {
            tcp->control = ACK_FLAG;
            tcb.need_ack = tcb.immediate_ack_req = false;
        }

        tcp->seq_num = to_net(tcb.SND_NXT);
        tcp->ack_num = to_net(tcb.RCV_NXT);

        pb->meta.tx_ref_cnt = 2;
        pb->meta.seq = tcb.SND_NXT;

        tcb.SND_NXT += payload_sz + !!(tcp->control & (SYN_FLAG | FIN_FLAG));

        tcb.tx_unacked.push_back(pb);
        if (tcb.tx_unacked.size() == 1)
            tcb.rto_deadline_cycles = io::cycle_timer::now() + io::cycle_timer::cycles_per_ms * RTO_MILLISECONDS;

        if constexpr (defer_doorbell)
            ctx.transmit_init(pb->dma_buf_addr, TCP_TOTAL_HDR_SZ + payload_sz, pb->id);
        else
            ctx.transmit(pb->dma_buf_addr, TCP_TOTAL_HDR_SZ + payload_sz, pb->id);
    }


    io::pkt_buf* socket::get_tx_buf() {
        if (ctx.tx_free_stk.empty())
            return nullptr;

        const int id = pop_back(ctx.tx_free_stk);
        io::pkt_buf* pb = ctx.tx_pkt_bufs[id];
        return pb;
    }

    io::tx_sgl socket::get_tx_sgl(int n_bytes) {
        io::tx_sgl sgl{};

        for (int left = n_bytes; left > 0 && !ctx.tx_free_stk.empty(); left -= TCP_MAX_PAYLOAD_SZ) {
            const int id = pop_back(ctx.tx_free_stk);
            io::pkt_buf* pb = ctx.tx_pkt_bufs[id];
            sgl.segments.push_back(pb);
            sgl.n_bytes += TCP_MAX_PAYLOAD_SZ;
        }

        return sgl;
    }

    u32 socket::generate_iss() {
        return 0;
    }

    void socket::refill_rx_ring() {
        int cnt = std::min<int>(ctx.receive_space(), ctx.rx_free_stk.size());
        cnt &= ~(io::REFILL_BATCH_SZ - 1);
        if (cnt == 0)
            return;
        for (int i = 0; i < cnt; ++i) {
            const int id = pop_back(ctx.rx_free_stk);
            ctx.receive_init(ctx.rx_pkt_bufs[id]->dma_buf_addr, id);
        }
        ctx.receive_push();
    }

    void socket::send_pure_ack() {
        if (ctx.tx_free_stk.empty() || ctx.transmit_space() == 0)
            return; // best effort

        const int id = pop_back(ctx.tx_free_stk);
        io::pkt_buf* pb = ctx.tx_pkt_bufs[id];

        net::get_ip_hdr(pb)->len = to_net<u16>(IP_HDR_SZ + TCP_HDR_SZ);

        auto* tcp = net::get_tcp_hdr(pb);
        tcp->control = ACK_FLAG;
        tcp->seq_num = to_net(tcb.SND_NXT);
        tcp->ack_num = to_net(tcb.RCV_NXT);

        tcb.need_ack = tcb.immediate_ack_req = false;
        pb->meta.tx_ref_cnt = 1;

        ctx.transmit(pb->dma_buf_addr, TCP_TOTAL_HDR_SZ, id);
    }

    void socket::poll() {
        ef_event events[io::POLL_BATCH_SZ];
        const int n_events = ctx.eventq_poll(events, io::POLL_BATCH_SZ);
        for (auto& event : events | std::views::take(n_events)) {
            switch (EF_EVENT_TYPE(event)) {
                case EF_EVENT_TYPE_RX_DISCARD: [[unlikely]] {
                    int id = EF_EVENT_RX_RQ_ID(event);
                    ctx.rx_free_stk.push_back(id);
                    LOG_DEBUG("tcp::socket::poll: EF_EVENT_TYPE_RX_DISCARD");
                    break;
                }
                case EF_EVENT_TYPE_RX: {
                    int id = EF_EVENT_RX_RQ_ID(event);
                    io::pkt_buf* pb = ctx.rx_pkt_bufs[id];
                    const net::tcp_hdr* tcp = net::get_tcp_hdr(pb);

                    // RST
                    if (tcp->control & RST_FLAG) [[unlikely]] {
                        ctx.rx_free_stk.push_back(id);
                        reset_tcb();
                        break;
                    }

                    // 3 WHS
                    if (tcb.state == fsm::SYN_SENT) [[unlikely]] {
                        if ((tcp->control & (SYN_FLAG | ACK_FLAG)) == (SYN_FLAG | ACK_FLAG) && from_net(tcp->ack_num) == tcb.ISS + 1) {
                            tcb.complete_handshake(from_net(tcp->seq_num), ctx.tx_free_stk);
                            send_pure_ack();
                        }
                        ctx.rx_free_stk.push_back(id);
                        break;
                    }

                    // ACK
                    if (tcp->control & ACK_FLAG) [[likely]]
                        tcb.handle_ack(from_net(tcp->ack_num), ctx.tx_free_stk);
                    else
                        LOG_DEBUG("Received segment without ACK");

                    auto payload = net::get_tcp_payload(pb);
                    if (payload.empty() && !(tcp->control & FIN_FLAG))
                        ctx.rx_free_stk.push_back(id);
                    else {
                        pb->meta = {payload, nullptr, from_net(tcp->seq_num)};
                        if (!tcb.rx_out_of_order.insert(pb))
                            ctx.rx_free_stk.push_back(id);
                    }
                    break;
                }
                case EF_EVENT_TYPE_TX_ERROR:
                    LOG_DEBUG("tcp::socket::poll: EF_EVENT_TYPE_TX_ERROR");
                    [[fallthrough]];
                case EF_EVENT_TYPE_TX: {
                    ef_request_id ids[EF_VI_TRANSMIT_BATCH];
                    const int n_ids = ctx.transmit_unbundle(event, ids);
                    for (auto id : ids | std::views::take(n_ids)) {
                        io::pkt_buf* pb = ctx.tx_pkt_bufs[id];
                        if (--pb->meta.tx_ref_cnt == 0)
                            ctx.tx_free_stk.push_back(id);
                    }
                    break;
                }
                default: {
                    LOG_DEBUG("Unknown event");
                    break;
                }
            }
        }

        tcb.process(ctx.rx_free_stk);

        if (tcb.immediate_ack_req || (tcb.need_ack && io::cycle_timer::now() >= tcb.ack_deadline_cycles))
            send_pure_ack();

        if (!tcb.tx_unacked.empty() && io::cycle_timer::now() > tcb.rto_deadline_cycles)
            retransmit_head();

        refill_rx_ring();
    }

    // caller should check if tcb.tx_unacked is empty before calling
    void socket::retransmit_head() {
        // best effort, retry in next poll
        if (ctx.transmit_space() == 0)
            return;

        io::pkt_buf* pb = tcb.tx_unacked.peek_front();
        net::tcp_hdr* tcp = net::get_tcp_hdr(pb);
        tcp->ack_num = to_net(tcb.RCV_NXT);
        ++(pb->meta.tx_ref_cnt);

        ctx.transmit(pb->dma_buf_addr, TCP_TOTAL_HDR_SZ + pb->meta.payload.size(), pb->id);

        tcb.rto_deadline_cycles = io::cycle_timer::now() + io::cycle_timer::cycles_per_ms * RTO_MILLISECONDS;
    }

    socket::~socket() {
        abort();
        ctx.teardown();
    }

    void socket::reset_tcb() {
        while (auto* pb = tcb.tx_unacked.pop_front())
            if (--pb->meta.tx_ref_cnt == 0)
                ctx.tx_free_stk.push_back(pb->id);
        while (auto* pb = tcb.rx_out_of_order.pop_front())
            ctx.rx_free_stk.push_back(pb->id);

        for (auto* pb = tcb.rx_ready_head; pb; ) {
            auto* nxt = pb->meta.nxt;
            ctx.rx_free_stk.push_back(pb->id);
            pb = nxt;
        }

        tcb = {};
    }
}
