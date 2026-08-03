#pragma once

#include <vector>

#include "context.h"

namespace io {
    struct rx_sgl {
        pkt_buf* head;
        pkt_buf* tail;
        int n_bytes;
    };
}
