#pragma once
#include <array>

#include "ef_vi_stuff.h"

class TxUnackedQueue {
public:
    pkt_buf* pop_front() {
        if (sz == 0)
            return nullptr;
        --sz;
        auto ret = queue[head];
        head = (head + 1) & (N_TX_BUFS - 1);
        return ret;
    }

    void push_back(pkt_buf* pb) {
        queue[tail] = pb;
        tail = (tail + 1) & (N_TX_BUFS - 1);
        ++sz;
    }

    [[nodiscard]] int size() const {
        return sz;
    }

    [[nodiscard]] pkt_buf* peek_front() const {
        if (sz == 0)
            return nullptr;
        return queue[head];
    }

    [[nodiscard]] bool empty() const {
        return sz == 0;
    }

private:
    std::array<pkt_buf*, N_TX_BUFS> queue{};
    int head = 0; // inclusive
    int tail = 0; // exclusive
    int sz = 0;
};
