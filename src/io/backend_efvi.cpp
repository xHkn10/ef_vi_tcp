#include <net/if.h>
#include <netinet/in.h>

#include <etherfabric/checksum.h>

#include "io/context.h"

namespace io {
    int context::init() {
       if (int rc = ef_driver_open(&dh); rc < 0) {
           LOG_ERROR("ef_driver_open: %s", std::strerror(-rc));
           return rc;
       }

        const auto ifindex = if_nametoindex(INTERFACE_NAME);
        if (int rc = ef_pd_alloc(&pd, dh, ifindex, EF_PD_DEFAULT); rc < 0) {
            LOG_ERROR("ef_pd_alloc: %s", std::strerror(-rc));
            return rc;
        }

        if constexpr (USE_CTPIO) {
            if (int rc = ef_vi_alloc_from_pd(&vi, dh, &pd, dh, -1, -1, -1, nullptr, -1, static_cast<enum ef_vi_flags>(EF_VI_TX_CTPIO)); rc < 0) {
                LOG_WARN("CTPIO VI alloc failed: %s", std::strerror(-rc));
                return rc;
            }
        } else {
            if (int rc = ef_vi_alloc_from_pd(&vi, dh, &pd, dh, -1, -1, -1, nullptr, -1, static_cast<enum ef_vi_flags>(0)); rc < 0) {
                LOG_ERROR("ef_vi_alloc_from_pd: %s", std::strerror(-rc));
                return rc;
            }
        }

        // SUPER IMPORTANT NOTE:
        // Default ef_vi_receive_buffer_len(&vi) size is 1792, which is 2048 - 256,
        // but we use only 64 bytes for pkt buf metadata, so we have to set this to 2048 - 64 = 1984,
        // so that frames of size (1792, 1984] don't arrive with scatter gather

        // std::printf("%d\n", ef_vi_receive_buffer_len(&vi));
        ef_vi_receive_set_buffer_len(&vi, BUF_SZ - PKT_BUF_MD_SZ);
        // std::printf("%d\n", ef_vi_receive_buffer_len(&vi));

        if (!mem_alloc())
            return -ENOMEM;

        if (int rc = ef_memreg_alloc(&rx_memreg, dh, &pd, dh, rx_mem, N_RX_BUFS * BUF_SZ); rc < 0) {
            LOG_ERROR("ef_memreg_alloc: %s", std::strerror(-rc));
            return rc;
        }

        if (int rc = ef_memreg_alloc(&tx_memreg, dh, &pd, dh, tx_mem, N_TX_BUFS * BUF_SZ); rc < 0) {
            LOG_ERROR("ef_memreg_alloc: %s", std::strerror(-rc));
            return rc;
        }

        buf_init<false>();

        return 0;
    }

    void context::teardown() {
        ef_memreg_free(&rx_memreg, dh);
        ef_memreg_free(&tx_memreg, dh);
        ef_vi_free(&vi, dh);
        ef_pd_free(&pd, dh);
        ef_driver_close(dh);
    }

    void context::transmit(pkt_buf* pb, int len) {
        if constexpr (USE_CTPIO) {
            auto* ip = net::get_ip_hdr(pb);
            auto* tcp = net::get_tcp_hdr(pb);
            ip->csum = ef_ip_checksum(reinterpret_cast<iphdr*>(ip));
            const iovec iov{pb->meta.payload.data(), pb->meta.payload.size()};
            tcp->checksum = ef_tcp_checksum(reinterpret_cast<iphdr*>(ip), reinterpret_cast<tcphdr*>(tcp), &iov, 1);
            ef_vi_transmit_ctpio(&vi, pb->dma_buf, len, CTPIO_THRESH);
            ef_vi_transmit_ctpio_fallback(&vi, pb->dma_buf_addr, len, pb->id);
        } else
            ef_vi_transmit(&vi, pb->dma_buf_addr, len, pb->id);
    }

    int context::eventq_poll(ef_event* events, int batch_sz) {
        return ef_eventq_poll(&vi, events, batch_sz);
    }

    void context::receive_init(ef_addr dma_buf_addr, int id) {
        ef_vi_receive_init(&vi, dma_buf_addr, id);
    }

    void context::receive_push() {
        ef_vi_receive_push(&vi);
    }

    int context::add_ip4_tcp_filter(u32 local_ip, u16 local_port) {
        ef_filter_spec spec;
        ef_filter_spec_init(&spec, EF_FILTER_FLAG_NONE);
        // ef_filter_spec_set_ip4_local expects network order
        if (int rc = ef_filter_spec_set_ip4_local(&spec, IPPROTO_TCP, to_net(local_ip), to_net(local_port)); rc < 0)
            return rc;
        return ef_vi_filter_add(&vi, dh, &spec, nullptr);
    }

    int context::transmit_space() {
        return ef_vi_transmit_space(&vi);
    }

    int context::receive_space() {
        return ef_vi_receive_space(&vi);
    }

    int context::transmit_unbundle(ef_event& event, ef_request_id* ids) {
        return ef_vi_transmit_unbundle(&vi, &event, ids);
    }
}
