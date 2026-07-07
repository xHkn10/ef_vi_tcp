#pragma once

#include <vector>

#include "context.h"

namespace io {
    struct rx_sgl {
        pkt_buf* head;
        pkt_buf* tail;
        int n_bytes;
    };

    struct tx_sgl {
        std::vector<pkt_buf*> segments;
        int n_bytes;
    };
}
