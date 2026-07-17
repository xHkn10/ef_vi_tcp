#include <algorithm>
#include <cstring>
#include <ranges>
#include <net/if.h>
#include <netinet/in.h>

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

        if (int rc = ef_vi_alloc_from_pd(&vi, dh, &pd, dh, -1, -1, -1, nullptr, -1, static_cast<enum ef_vi_flags>(0)); rc < 0) {
            LOG_ERROR("ef_vi_alloc_from_pd: %s", std::strerror(-rc));
            return rc;
        }

        if (int rc = mem_alloc(); rc)
            return rc;

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

    void context::transmit(ef_addr dma_buf_addr, int len, int id) {
        ef_vi_transmit(&vi, dma_buf_addr, len, id);
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

    void context::transmit_init(ef_addr dma_buf_addr, int len, int id) {
        ef_vi_transmit_init(&vi, dma_buf_addr, len, id);
    }

    void context::transmit_push() {
        ef_vi_transmit_push(&vi);
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
