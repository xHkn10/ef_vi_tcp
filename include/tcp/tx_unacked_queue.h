#pragma once

#include <array>

#include "io/efvi_stuff.h"

namespace tcp {
    class tx_unacked_queue {
    public:
        io::pkt_buf* pop_front() {
            if (sz == 0)
                return nullptr;
            --sz;
            auto ret = queue[head];
            head = (head + 1) & (io::N_TX_BUFS - 1);
            return ret;
        }

        void push_back(io::pkt_buf* pb) {
            queue[tail] = pb;
            tail = (tail + 1) & (io::N_TX_BUFS - 1);
            ++sz;
        }

        [[nodiscard]] int size() const {
            return sz;
        }

        [[nodiscard]] io::pkt_buf* peek_front() const {
            if (sz == 0)
                return nullptr;
            return queue[head];
        }

        [[nodiscard]] bool empty() const {
            return sz == 0;
        }

    private:
        std::array<io::pkt_buf*, io::N_TX_BUFS> queue{};
        int head = 0; // inclusive
        int tail = 0; // exclusive
        int sz = 0;
    };
}
