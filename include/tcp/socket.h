#pragma once

#include "io/context.h"
#include "tcb.h"
#include "types.h"
#include "io/sgl.h"

namespace tcp {
    class socket {
    public:
        socket();
        ~socket();

        socket(const socket&) = delete;
        socket& operator=(const socket&) = delete;

        [[nodiscard]] bool is_closed() const { return tcb.state == fsm::CLOSED; }
        [[nodiscard]] bool is_established() const { return tcb.state == fsm::ESTABLISHED; }

        bool bind(u32 local_ip, u16 local_port, u32 remote_ip, u16 remote_port, u8 dmac[6], u8 smac[6]);
        bool connect();

        bool close();
        bool abort();

        void poll();

        io::pkt_buf* receive_single();
        [[nodiscard]] io::rx_sgl receive_available();
        int receive(std::span<std::byte>);

        int consume(const io::rx_sgl&, int bytes_to_consume);

        bool send(io::pkt_buf*);
        bool send(io::tx_sgl&&);
        int send(std::span<const std::byte> payload);

        io::pkt_buf* get_tx_buf();
        io::tx_sgl get_tx_sgl(int n_bytes);

    private:
        void reset_tcb();

        void refill_rx_ring();

        static u32 generate_iss();
        void write_headers(io::pkt_buf*) const;

        template <bool defer_doorbell, bool stamp_ack_only>
        void stamp_and_send(io::pkt_buf*);

        void send_pure_ack();

        void retransmit_head();

        io::context ctx;

        TCB tcb;

        u32 local_ip;
        u16 local_port;
        u32 remote_ip;
        u16 remote_port;

        bool is_bound;

#ifdef TCP_TEST_HOOKS
    public:
        io::context& test_ctx() { return ctx; }
        TCB& test_tcb() { return tcb; }
#endif
    };
}
