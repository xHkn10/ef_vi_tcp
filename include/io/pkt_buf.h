#pragma  once

#include <etherfabric/ef_vi.h>

#include <cstddef>
#include <span>

#include "types.h"

namespace io {
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
        void set_payload_sz(u32 payload_sz) {
            meta.payload = {meta.payload.data(), payload_sz};
        }
    };
}
