#include <deque>

#include "io/context.h"
#include "io/backend_test.h"

namespace {
    template <typename T>
    T pop_front(std::deque<T>& dq) {
        T item = dq.front();
        dq.pop_front();
        return item;
    }
}

namespace {
    std::vector<std::vector<std::byte>> g_captured;

    std::deque<int> g_rx_avail; // garbage data
    std::deque<int> g_rx_pending; // holds data

    std::deque<int> g_tx_done;
}

namespace io::test {
    void test_inject(context& ctx, std::span<const std::byte> frame) {
        int id = pop_front(g_rx_avail);
        auto* pb = ctx.rx_pkt_bufs[id];
        std::memcpy(pb->dma_buf, frame.data(), frame.size());
        g_rx_pending.push_back(id);
    }

    void test_reset() {
        g_captured.clear();
        g_rx_avail.clear();
        g_rx_pending.clear();
        g_tx_done.clear();
    }

    std::vector<std::vector<std::byte>>& test_captured() {
        return g_captured;
    }
}


namespace io {
    int context::init() {
        if (int rc = mem_alloc(); rc)
            return rc;
        buf_init<true>();
        return 0;
    }

    void context::teardown() {}

    void context::transmit(ef_addr, int len, int id) {
        auto* pb = tx_pkt_bufs[id];
        auto* p = reinterpret_cast<std::byte*>(pb->dma_buf);
        g_captured.emplace_back(p, p + len);
        g_tx_done.push_back(id);
    }

    void context::transmit_init(ef_addr addr, int len, int id) {
        transmit(addr, len, id);
    }

    void context::transmit_push() {}

    int context::transmit_space() {
        return N_TX_BUFS;
    }

    int context::transmit_unbundle(ef_event& event, ef_request_id* ids) {
        ids[0] = event.tx.desc_id;
        return 1;
    }

    void context::receive_init(ef_addr, int id) {
        g_rx_avail.push_back(id);
    }

    void context::receive_push() {}

    int context::receive_space() {
        return N_RX_BUFS;
    }

    int context::add_ip4_tcp_filter(u32, u16) {
        return 0;
    }

    int context::eventq_poll(ef_event* events, int batch_sz) {
        int n = 0;
        for (; !g_rx_pending.empty() && n < batch_sz; ++n) {
            int id = pop_front(g_rx_pending);
            ef_event ev{};
            ev.rx.type = EF_EVENT_TYPE_RX;
            ev.rx.rq_id = id;
            events[n] = ev;
        }
        for (; !g_tx_done.empty() && n < batch_sz; ++n) {
            int id = pop_front(g_tx_done);
            ef_event ev{};
            ev.tx.desc_id = id;
            ev.tx.type = EF_EVENT_TYPE_TX;
            events[n] = ev;
        }
        return n;
    }
}
