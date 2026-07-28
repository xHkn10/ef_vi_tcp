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
        if (const int rc = ctx.init(); rc < 0)
            throw std::runtime_error("socket couldn't initialized, rc = " + std::to_string(rc));

        tcb = {};
        local_ip = 0;
        local_port = 0;
        remote_ip = 0;
        remote_port = 0;
        is_bound = false;
        is_listener = false;
    }

    bool socket::bind(u32 local_ip, u16 local_port, u32 remote_ip, u16 remote_port, std::array<u8, 6> dmac, std::array<u8, 6> smac) {
        if (int rc = ctx.add_ip4_tcp_filter(local_ip, local_port); rc < 0) {
            LOG_ERROR("add_ip4_tcp_filter: %s", strerror(-rc));
            return false;
        }

        this->local_ip = local_ip;
        this->local_port = local_port;
        this->remote_ip = remote_ip;
        this->remote_port = remote_port;

        for (auto& pb : ctx.tx_pkt_bufs) {
            auto* eth = net::get_eth_hdr(pb);
            auto* tcp = net::get_tcp_hdr(pb);
            auto* ip = net::get_ip_hdr(pb);

            *eth = {
                dmac,
                smac,
                to_net<u16>(0x0800)
            };

            *ip = {
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

            *tcp = {
                to_net(local_port),
                to_net(remote_port),
                to_net(tcb.ISS),
                0,
                TCP_DEFAULT_DOFFSET_RESERVED,
                0, // CWR, ECE, URG, ACK, PSH, RST, SYN, FIN; since this is a varying field, it should be always set explicitly
                to_net<u16>(0xFFFF),
                0,
                0
            };

            pb->meta = {{pb->dma_buf + TCP_TOTAL_HDR_SZ, 0}, nullptr, 0, 2};
        }

        is_bound = true;

        return true;
    }

    void socket::listen() {
        is_listener = true;
        tcb.state = fsm::LISTEN;
    }

    void socket::accept_syn(io::pkt_buf* rx) {
        if (ctx.tx_free_stk.empty()) [[unlikely]]
            return;

        const auto peer_opts = net::parse_tcp_options(net::get_tcp_options(rx));
        set_snd_mss(peer_opts.mss);

        tcb.ISR = from_net(net::get_tcp_hdr(rx)->seq_num);
        tcb.RCV_NXT = tcb.ISR + 1;
        tcb.ISS = generate_iss();
        tcb.SND_UNA = tcb.SND_NXT = tcb.ISS;
        tcb.state = fsm::SYN_RECEIVED;

        const int id = pop_back(ctx.tx_free_stk);
        io::pkt_buf* pb = ctx.tx_pkt_bufs[id];
        pb->set_payload_sz(0);

        net::get_tcp_hdr(pb)->control = SYN_FLAG | ACK_FLAG;
        const auto opt_len = net::write_mss_option(pb, TCP_MAX_PAYLOAD_SZ);

        stamp_and_send<false>(pb, opt_len);
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

        const auto opt_len = net::write_mss_option(pb, TCP_MAX_PAYLOAD_SZ);

        net::get_tcp_hdr(pb)->control = SYN_FLAG;

        stamp_and_send<false>(pb, opt_len);

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

        stamp_and_send<false>(pb, 0);

        return true;
    }

    bool socket::abort() {
        if (tcb.state == fsm::CLOSED)
            return false;
        send_rst(to_net(tcb.SND_NXT), to_net(tcb.RCV_NXT));
        reset_tcb();
        return true;
    }

    bool socket::send(io::pkt_buf* pb) {
        if (tcb.state != fsm::ESTABLISHED || ctx.transmit_space() == 0)
            return false;
        stamp_and_send<true>(pb, 0);
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
            stamp_and_send<true>(seg, 0);

        return true;
    }

    int socket::send(std::span<const std::byte> payload) {
        if (tcb.state != fsm::ESTABLISHED || payload.empty()) [[unlikely]]
            return 0;

        int n_bytes_sent = 0;

        while (n_bytes_sent < payload.size() && !ctx.tx_free_stk.empty() && ctx.transmit_space() > 0) {
            if (const u32 in_flight = tcb.SND_NXT - tcb.SND_UNA; in_flight >= tcb.SND_WND)
                break;
            const int id = pop_back(ctx.tx_free_stk);
            io::pkt_buf* pb = ctx.tx_pkt_bufs[id];

            const int chunk = std::min<int>(tcb.snd_mss, payload.size() - n_bytes_sent);
            std::memcpy(pb->dma_buf + TCP_TOTAL_HDR_SZ, payload.data() + n_bytes_sent, chunk);
            pb->set_payload_sz(chunk);

            stamp_and_send<true>(pb,0);

            n_bytes_sent += chunk;
        }

        return n_bytes_sent;
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

    template <bool stamp_ack_only>
    void socket::stamp_and_send(io::pkt_buf* pb, int opt_len) {
        const int payload_sz = pb->meta.payload.size();
        const int tcp_hdr_len = TCP_HDR_SZ + opt_len;

        auto* tcp = net::get_tcp_hdr(pb);
        auto* ip = net::get_ip_hdr(pb);

        tcp->doffset_reserved = (tcp_hdr_len >> 2) << 4;
        ip->len = to_net<u16>(IP_HDR_SZ + tcp_hdr_len + payload_sz);

        if constexpr (stamp_ack_only) {
            tcp->control = ACK_FLAG;
            tcb.need_ack = tcb.immediate_ack_req = false;
            tcb.segs_since_ack = 0;
        }

        tcp->seq_num = to_net(tcb.SND_NXT);
        tcp->ack_num = to_net(tcb.RCV_NXT);

        pb->meta.tx_ref_cnt = 2;
        pb->meta.seq = tcb.SND_NXT;

        tcb.SND_NXT += payload_sz + !!(tcp->control & (SYN_FLAG | FIN_FLAG));

        tcb.tx_unacked.push_back(pb);
        if (tcb.tx_unacked.size() == 1)
            tcb.rto_deadline_cycles = io::cycle_timer::now() + io::cycle_timer::cycles_per_ms * RETRANSMISSION_TIMEOUT_MILLISECONDS;

        ctx.transmit(pb, ETH_HDR_SZ + IP_HDR_SZ + tcp_hdr_len + payload_sz);
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

        for (int left = n_bytes; left > 0 && !ctx.tx_free_stk.empty(); left -= tcb.snd_mss) {
            const int id = pop_back(ctx.tx_free_stk);
            io::pkt_buf* pb = ctx.tx_pkt_bufs[id];
            sgl.segments.push_back(pb);
            sgl.n_bytes += tcb.snd_mss;
        }

        return sgl;
    }

    u32 socket::generate_iss() {
#ifdef TCP_TEST_HOOKS
        return 0;
#endif
        const auto micro_second_clocks = io::cycle_timer::cycles_per_ms / 1000;
        const u32 iss = io::cycle_timer::now() / (4 * micro_second_clocks);
        return iss;
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

        auto* tcp = net::get_tcp_hdr(pb);
        auto* ip = net::get_ip_hdr(pb);

        ip->len = to_net<u16>(IP_HDR_SZ + TCP_HDR_SZ);

        tcp->control = ACK_FLAG;
        tcp->seq_num = to_net(tcb.SND_NXT);
        tcp->ack_num = to_net(tcb.RCV_NXT);
        tcp->doffset_reserved = (TCP_HDR_SZ >> 2) << 4;

        tcb.need_ack = tcb.immediate_ack_req = false;
        tcb.segs_since_ack = 0;
        pb->meta.tx_ref_cnt = 1;

        ctx.transmit(pb, TCP_TOTAL_HDR_SZ);
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

                    if (tcb.state == fsm::CLOSED) [[unlikely]] {
                        ctx.rx_free_stk.push_back(id);
                        send_rst(to_net(tcp->ack_num), 0);
                        break;
                    }

                    if constexpr (ENABLE_PASSIVE_OPEN) {
                        if (tcb.state == fsm::LISTEN) [[unlikely]] {
                            if (tcp->control & SYN_FLAG)
                                accept_syn(pb);
                            ctx.rx_free_stk.push_back(id);
                            break;
                        }
                        if (tcb.state == fsm::SYN_RECEIVED) [[unlikely]] {
                            if ((tcp->control & ACK_FLAG) && from_net(tcp->ack_num) == tcb.ISS + 1) {
                                tcb.handle_ack(tcb.ISS + 1, ctx.tx_free_stk); // remove SYN|ACK from tx_unacked
                                tcb.state = fsm::ESTABLISHED;
                            }
                            ctx.rx_free_stk.push_back(id);
                            break;
                        }
                    }

                    // RST
                    if (tcp->control & RST_FLAG) [[unlikely]] {
                        ctx.rx_free_stk.push_back(id);
                        reset_tcb();
                        if constexpr (ENABLE_PASSIVE_OPEN)
                            if (is_listener)
                                tcb.state = fsm::LISTEN;
                        break;
                    }

                    // 3 WHS
                    if (tcb.state == fsm::SYN_SENT) [[unlikely]] {
                        if ((tcp->control & (SYN_FLAG | ACK_FLAG)) == (SYN_FLAG | ACK_FLAG) && from_net(tcp->ack_num) == tcb.ISS + 1) [[likely]] {
                            tcb.SND_WND = from_net(tcp->window);
                            set_snd_mss(net::parse_tcp_options(net::get_tcp_options(pb)).mss);
                            tcb.complete_handshake(from_net(tcp->seq_num), ctx.tx_free_stk);
                            send_pure_ack();
                        } else if (tcp->control & ACK_FLAG) [[unlikely]] {
                            // Sometimes a stale 4 tuple just persists, and as a result netcat sends a segment with
                            // ACK. This is called called a challenge ACK
                            send_rst(to_net(tcp->ack_num), 0);
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
                        pb->meta = {payload, nullptr, from_net(tcp->seq_num), 0};
                        if (!tcb.accept_in_order(pb)) {
                            tcb.immediate_ack_req = true;
                            if (!tcb.rx_out_of_order.insert(pb))
                                ctx.rx_free_stk.push_back(id);
                        }
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

#ifdef TCP_TEST_HOOKS
        const auto cur_time = io::cycle_timer::now();
#else
        const auto cur_time = poll_counter++ & 0xFF ? 0LL : io::cycle_timer::now();
#endif

        if (tcb.immediate_ack_req || (tcb.need_ack && cur_time >= tcb.d_ack_deadline_cycles))
            send_pure_ack();

        if (!tcb.tx_unacked.empty() && cur_time >= tcb.rto_deadline_cycles)
            retransmit_head();

        if (tcb.state == fsm::TIME_WAIT && cur_time >= tcb.tw_deadline_cycles) [[unlikely]]
            reset_tcb();

        refill_rx_ring();
    }

    // caller should check if tcb.tx_unacked is empty before calling
    void socket::retransmit_head() {
        // best effort, retry in next poll
        // Note: We are NOT strictly retransmitting from SND_UNA when we get a partial ACK,
        // but potentially a little before from it, from the start of a segment
        // notebook lm says this is OK
        if (ctx.transmit_space() == 0)
            return;

        auto* pb = tcb.tx_unacked.peek_front();
        auto* tcp = net::get_tcp_hdr(pb);
        tcp->ack_num = to_net(tcb.RCV_NXT);
        ++(pb->meta.tx_ref_cnt);

        ctx.transmit(pb, ETH_HDR_SZ + from_net(net::get_ip_hdr(pb)->len));

        tcb.rto_deadline_cycles = io::cycle_timer::now() + io::cycle_timer::cycles_per_ms * RETRANSMISSION_TIMEOUT_MILLISECONDS;
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

        tcb = {}; // sets state to closed
    }

    void socket::set_snd_mss(u16 mss) {
        const auto peer_mss = mss ? mss : TCP_DEFAULT_MSS;
        tcb.snd_mss = std::min<u16>(TCP_MAX_PAYLOAD_SZ, peer_mss);
    }

    void socket::send_rst(u32 seq, u32 ack) {
        if (ctx.tx_free_stk.empty() || ctx.transmit_space() == 0)
            return;
        const int id = pop_back(ctx.tx_free_stk);
        io::pkt_buf* pb = ctx.tx_pkt_bufs[id];
        auto* tcp = net::get_tcp_hdr(pb);
        auto* ip  = net::get_ip_hdr(pb);

        tcp->control = RST_FLAG;
        tcp->seq_num = seq;
        tcp->ack_num = ack;
        tcp->doffset_reserved = TCP_DEFAULT_DOFFSET_RESERVED;
        ip->len = to_net<u16>(IP_HDR_SZ + TCP_HDR_SZ);

        pb->meta.tx_ref_cnt = 1;
        ctx.transmit(pb, TCP_TOTAL_HDR_SZ);
    }
}
