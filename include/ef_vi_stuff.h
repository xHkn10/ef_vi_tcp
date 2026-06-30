#pragma once

#include <algorithm>
#include <etherfabric/vi.h>
#include <etherfabric/memreg.h>
#include <etherfabric/pd.h>

#include <array>
#include <ranges>
#include <vector>
#include <cstring>

#include "config.h"
#include "types.h"

struct pkt_buf {
    ef_addr dma_buf_addr;
    int id;
    struct {
        std::span<std::byte> payload;
        pkt_buf* nxt;
        u32 seq;
        int tx_ref_cnt;
    } meta;
    std::byte dma_buf[1] __attribute__((aligned(EF_VI_DMA_ALIGN/*align for 64 bytes cache sz*/)));
    // 2048 - 64 = 1984 bytes left
    bool operator<(const pkt_buf& o) const {
        return seq_less(meta.seq, o.meta.seq);
    }
};

struct ef_struct {
    ef_driver_handle dh;
    ef_vi vi;
    ef_pd pd;

    ef_memreg rx_memreg;
    ef_memreg tx_memreg;

    void* rx_mem;
    void* tx_mem;

    std::vector<int> tx_free_stk;
    std::vector<int> rx_free_stk;

    std::array<pkt_buf*, N_RX_BUFS> rx_pkt_bufs{};
    std::array<pkt_buf*, N_TX_BUFS> tx_pkt_bufs{};

    int init_ef_vi() {
        if (int rc = ef_driver_open(&dh); rc < 0) {
            fprintf(stderr, "ef_driver_open: %s\n", strerror(-rc));
            return rc;
        }

        if (int rc = ef_pd_alloc(&pd, dh, -1, EF_PD_DEFAULT); rc < 0) {
            fprintf(stderr, "ef_pd_alloc: %s\n", strerror(-rc));
            return rc;
        }

        if (int rc = ef_vi_alloc_from_pd(&vi, dh, &pd, dh, -1, -1, -1, nullptr, -1, static_cast<enum ef_vi_flags>(0)); rc < 0) {
            fprintf(stderr, "ef_vi_alloc_from_pd: %s\n", strerror(-rc));
            return rc;
        }

        if (int rc = posix_memalign(&rx_mem, PAGE_SZ, N_RX_BUFS * BUF_SZ); rc != 0) {
            fprintf(stderr, "posix_memalign: %s\n", strerror(-rc));
            return rc;
        }

        if (int rc = posix_memalign(&tx_mem, PAGE_SZ, N_TX_BUFS * BUF_SZ); rc != 0) {
            fprintf(stderr, "posix_memalign: %s\n", strerror(-rc));
            return rc;
        }

        if (int rc = ef_memreg_alloc(&rx_memreg, dh, &pd, dh, rx_mem, N_RX_BUFS * BUF_SZ); rc < 0) {
            fprintf(stderr, "ef_memreg_alloc: %s\n", strerror(-rc));
            return rc;
        }

        if (int rc = ef_memreg_alloc(&tx_memreg, dh, &pd, dh, tx_mem, N_TX_BUFS * BUF_SZ); rc < 0) {
            fprintf(stderr, "ef_memreg_alloc: %s\n", strerror(-rc));
            return rc;
        }

        rx_free_stk.reserve(N_RX_BUFS);
        tx_free_stk.reserve(N_TX_BUFS);

        for (int i = 0; i < N_RX_BUFS; ++i) {
            auto* pb = reinterpret_cast<pkt_buf*>(static_cast<char*>(rx_mem) + i * BUF_SZ);
            pb->dma_buf_addr = ef_memreg_dma_addr(&rx_memreg, i * BUF_SZ) + offsetof(pkt_buf, dma_buf);
            pb->id = i;
            rx_pkt_bufs[i] = pb;
        }

        for (int i = 0; i < N_TX_BUFS; ++i) {
            auto* pb = reinterpret_cast<pkt_buf*>(static_cast<char*>(tx_mem) + i * BUF_SZ);
            pb->dma_buf_addr = ef_memreg_dma_addr(&tx_memreg, i * BUF_SZ) + offsetof(pkt_buf, dma_buf);
            pb->id = i;
            tx_pkt_bufs[i] = pb;
        }

        int rx_fill_cnt = std::min<int>(ef_vi_receive_space(&vi), N_RX_BUFS);

        for (auto& pb : rx_pkt_bufs | std::views::take(rx_fill_cnt))
            ef_vi_receive_init(&vi, pb->dma_buf_addr, pb->id);
        ef_vi_receive_push(&vi);

        rx_free_stk.resize(N_RX_BUFS - rx_fill_cnt);
        std::ranges::iota(rx_free_stk, rx_fill_cnt);

        tx_free_stk.resize(N_TX_BUFS);
        std::ranges::iota(tx_free_stk, 0);

        return 0;
    }

    void teardown() {
        ef_memreg_free(&rx_memreg, dh);
        ef_memreg_free(&tx_memreg, dh);
        ef_vi_free(&vi, dh);
        ef_pd_free(&pd, dh);
        ef_driver_close(dh);
    }
};
