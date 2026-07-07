#pragma once

#include "io/cycle_timer.h"
#include "net/net_headers.h"
#include "rx_ooo_list.h"
#include "io/sgl.h"
#include "tx_unacked_queue.h"
#include "types.h"

namespace tcp {
    enum class TcpState : u32 {
        LISTEN,
        SYN_SENT,
        SYN_RECEIVED,
        ESTABLISHED,
        FIN_WAIT1,
        FIN_WAIT2,
        CLOSE_WAIT,
        CLOSING,
        LAST_ACK,
        TIME_WAIT,
        CLOSED
    };

    struct seg_cmp {
        bool operator()(const io::pkt_buf* a, const io::pkt_buf* b) const {
            return *a < *b;
        }
    };

    struct TCB {
        io::pkt_buf* rx_ready_head;
        io::pkt_buf* rx_ready_tail;
        int ready_bytes;

        rx_ooo_list rx_out_of_order;
        tx_unacked_queue tx_unacked;

        TcpState state;

        u32 SND_UNA;
        u32 SND_NXT;
        u32 SND_WND;

        u32 RCV_NXT;
        u32 RCV_WND;

        u32 ISS;
        u32 ISR;

        bool need_ack;
        bool immediate_ack_req;
        u64 ack_deadline_cycles;
        u64 rto_deadline_cycles;

        io::rx_sgl hand_out_ready() {
            auto* tmp_head = rx_ready_head;
            auto* tmp_tail = rx_ready_tail;
            auto n = ready_bytes;
            rx_ready_head = rx_ready_tail = nullptr;
            ready_bytes = 0;
            return {tmp_head, tmp_tail, n};
        }

        io::pkt_buf* pop_front() {
            if (!rx_ready_head)
                return nullptr;
            io::pkt_buf* ret = rx_ready_head;
            if (rx_ready_head == rx_ready_tail)
                rx_ready_head = rx_ready_tail = nullptr;
            else
                rx_ready_head = rx_ready_head->meta.nxt;
            ready_bytes -= ret->meta.payload.size();
            return ret;
        }

        void process(std::vector<int>& rx_free_stk) {
            while (!rx_out_of_order.empty()) {
                io::pkt_buf* rx_seg = rx_out_of_order.peek_front();
                net::tcp_hdr* tcp = net::get_tcp_hdr(rx_seg);

                if (rx_seg->meta.seq == RCV_NXT) {
                    if (!rx_seg->meta.payload.empty()) {
                        append_ready(rx_seg);
                        ready_bytes += static_cast<int>(rx_seg->meta.payload.size());
                        RCV_NXT += rx_seg->meta.payload.size();

                        queue_ack();
                    }

                    // remote side is closing
                    if (tcp->control & FIN_FLAG) {
                        ++RCV_NXT;
                        handle_fin();
                        if (rx_seg->meta.payload.empty())
                            rx_free_stk.push_back(rx_seg->id);
                    }

                    rx_out_of_order.pop_front();
                } else if (seq_less(rx_seg->meta.seq, RCV_NXT)) { // TODO what if chunk of the data partially overlaps?
                    immediate_ack_req = true;
                    rx_free_stk.push_back(rx_seg->id);
                    rx_out_of_order.pop_front();
                }
                else {
                    immediate_ack_req = true;
                    break;
                }
            }
        }

        void handle_ack(u32 ack_num, std::vector<int>& tx_free_stk) {
            if ((ack_num - SND_UNA) <= (SND_NXT - SND_UNA))
                SND_UNA = ack_num;

            bool acked_any = false;
            while (io::pkt_buf* seg = tx_unacked.peek_front()) {
                u32 seq_end = seg->meta.seq + seg->meta.payload.size() + !!(net::get_tcp_hdr(seg)->control & FIN_FLAG);
                if (seq_less(SND_UNA, seq_end))
                    break;

                if (--seg->meta.tx_ref_cnt == 0)
                    tx_free_stk.push_back(seg->id);

                acked_any = true;
                tx_unacked.pop_front();
            }

            if (acked_any) {
                if (tx_unacked.empty())
                    rto_deadline_cycles = 0;
                else
                    rto_deadline_cycles = io::cycle_timer::now() + io::cycle_timer::cycles_per_ms * RTO_MILLISECONDS;
            }

            // our FIN might be acknowledged
            if (SND_UNA == SND_NXT) {
                if (state == TcpState::FIN_WAIT1)
                    state = TcpState::FIN_WAIT2;
                else if (state == TcpState::CLOSING)
                    state = TcpState::TIME_WAIT;
                else if (state == TcpState::LAST_ACK)
                    state = TcpState::CLOSED;
            }
        }

    private:
        void append_ready(io::pkt_buf* rx_seg) {
            if (rx_ready_head == nullptr)
                rx_ready_head = rx_ready_tail = rx_seg;
            else {
                rx_ready_tail->meta.nxt = rx_seg;
                rx_ready_tail = rx_seg;
            }
            rx_ready_tail->meta.nxt = nullptr;
        }

        void handle_fin() {
            switch (state) {
                case TcpState::ESTABLISHED:
                    state = TcpState::CLOSE_WAIT;
                    break;
                case TcpState::FIN_WAIT1:
                    // simultaneous close
                    state = TcpState::CLOSING;
                    break;
                case TcpState::FIN_WAIT2:
                    state = TcpState::TIME_WAIT;
                    break;
                default:
                    break;
            }

            immediate_ack_req = true;
        }

        void queue_ack() {
            if (!need_ack) {
                need_ack = true;
                ack_deadline_cycles = io::cycle_timer::now() + io::cycle_timer::cycles_per_ms * 100;
            }
        }
    };
}
