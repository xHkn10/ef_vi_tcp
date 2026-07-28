#pragma once

#include <cstddef>
#include <span>
#include <deque>

#include "io/context.h"

namespace io::test {
    inline std::vector<std::vector<std::byte>> g_sent_captured;
    inline std::deque<int> g_rx_avail; // garbage data
    inline std::deque<int> g_rx_pending; // holds data
    inline std::deque<int> g_tx_done;

    void test_inject(context& ctx, std::span<const std::byte> frame);
    void test_reset();
}
