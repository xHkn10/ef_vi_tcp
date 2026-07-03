#pragma once

#include <vector>

#include "ef_vi_stuff.h"

struct rx_sgl {
    pkt_buf* head{};
    pkt_buf* tail{};
    int n_bytes{};

    void append(pkt_buf* seg) {
        if (!tail)
            head = tail = seg;
        else {
            tail->meta.nxt = seg;
            tail = seg;
        }
    }
};

struct tx_sgl {
    std::vector<pkt_buf*> segments{};
    int n_bytes{};
};
