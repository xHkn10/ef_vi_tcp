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

        bool bind(u32 local_ip, u16 local_port);
        bool connect(u32 remote_ip, u16 remote_port, u8 dmac[6], u8 smac[6]);

        bool close();
        bool abort();

        io::pkt_buf* receive_single();
        io::rx_sgl receive_available();
        int consume(const io::rx_sgl&, int bytes_to_consume);
        int receive(std::span<std::byte>);

        bool send(io::pkt_buf*);
        bool send(io::tx_sgl&&);
        int send(std::span<const std::byte> payload);

        io::pkt_buf* get_tx_buf();
        io::tx_sgl get_tx_sgl(int n_bytes);

    private:
        void refill_rx_ring();

        static u32 generate_iss();
        void write_headers(io::pkt_buf*) const;

        template <bool queue>
        void stamp_and_send(io::pkt_buf*, int payload_sz);

        void send_pure_ack();

        void poll_once();

        void retransmit_head();

        io::context ctx;

        TCB tcb;

        u32 local_ip;
        u16 local_port;
        u32 remote_ip;
        u16 remote_port;

        bool is_bound;
    };
}
