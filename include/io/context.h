#pragma once

#include <sys/mman.h>

#include <etherfabric/vi.h>
#include <etherfabric/memreg.h>
#include <etherfabric/pd.h>

#include <array>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <ranges>
#include <algorithm>

#include "config.h"
#include "pkt_buf.h"
#include "net/net_headers.h"

namespace io {
    struct context {
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

        int init();
        void teardown();

        int add_ip4_tcp_filter(u32 local_ip, u16 local_port);
        int transmit_space();
        int receive_space();
        int transmit_unbundle(ef_event& event, ef_request_id* ids);

        void transmit(ef_addr dma_buf_addr, int len, int id);
        void transmit_push();
        void transmit_init(ef_addr dma_buf_addr, int len, int id);
        void receive_init(ef_addr dma_buf_addr, int id);
        void receive_push();
        int eventq_poll(ef_event* events, int batch_sz);

    private:
        bool mem_alloc() {
            auto huge_alloc = [](const u64 sz) -> void* {
                const size_t huge_sz = (sz + HUGEPAGE_SZ - 1) / HUGEPAGE_SZ * HUGEPAGE_SZ;
                void* p = mmap(nullptr, huge_sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
                if (p == MAP_FAILED) {
                    LOG_WARN("hugepage alloc failed: %s, falling back to 4K pages", std::strerror(errno));
                    if (const int rc = posix_memalign(&p, HUGEPAGE_SZ, sz); rc != 0) {
                        LOG_ERROR("posix_memalign also failed: %s", std::strerror(rc));
                        p = nullptr;
                    }
                }
                return p;
            };

            rx_mem = huge_alloc(N_RX_BUFS * BUF_SZ);
            tx_mem = huge_alloc(N_TX_BUFS * BUF_SZ);

            rx_free_stk.reserve(N_RX_BUFS);
            tx_free_stk.reserve(N_TX_BUFS);

            return (rx_mem != nullptr) && (tx_mem != nullptr);
        }

        template <bool fake>
        void buf_init() {
            for (int i = 0; i < N_RX_BUFS; ++i) {
                auto* pb = reinterpret_cast<pkt_buf*>(static_cast<char*>(rx_mem) + i * BUF_SZ);
                if constexpr (fake)
                    pb->dma_buf_addr = 0;
                else
                    pb->dma_buf_addr = ef_memreg_dma_addr(&rx_memreg, i * BUF_SZ) + offsetof(pkt_buf, dma_buf);
                pb->id = i;
                pb->meta.payload = {pb->dma_buf + TCP_TOTAL_HDR_SZ, 0};
                rx_pkt_bufs[i] = pb;
            }

            for (int i = 0; i < N_TX_BUFS; ++i) {
                auto* pb = reinterpret_cast<pkt_buf*>(static_cast<char*>(tx_mem) + i * BUF_SZ);
                if constexpr (fake)
                    pb->dma_buf_addr = 0;
                else
                    pb->dma_buf_addr = ef_memreg_dma_addr(&tx_memreg, i * BUF_SZ) + offsetof(pkt_buf, dma_buf);
                pb->id = i;
                pb->meta.payload = {pb->dma_buf + TCP_TOTAL_HDR_SZ, 0};
                tx_pkt_bufs[i] = pb;
            }

            int rx_fill_cnt = std::min<int>(receive_space(), N_RX_BUFS);

            for (const auto& pb : rx_pkt_bufs | std::views::take(rx_fill_cnt))
                receive_init(pb->dma_buf_addr, pb->id);
            receive_push();

            rx_free_stk.resize(N_RX_BUFS - rx_fill_cnt);
            std::ranges::iota(rx_free_stk, rx_fill_cnt);

            tx_free_stk.resize(N_TX_BUFS);
            std::ranges::iota(tx_free_stk, 0);
        }
    };
}
