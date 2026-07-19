#pragma once

#include <cstdio>
#include <etherfabric/ef_vi.h>

#define LOG_ERROR(fmt, ...) \
    fprintf(stderr, "[ERROR] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    fprintf(stderr, "[WARN]  %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) \
    fprintf(stdout, "[INFO]  %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#ifdef DEBUG
    #define LOG_DEBUG(fmt, ...) \
        fprintf(stderr, "[DEBUG] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
    #define LOG_DEBUG(fmt, ...) ((void)0)
#endif

namespace io {
    constexpr int BUF_SZ = 2048;
    constexpr int HUGEPAGE_SZ = 2 * 1024 * 1024;
    constexpr int N_RX_BUFS = 1024;
    constexpr int N_TX_BUFS = 1024;
    constexpr int N_BUFS = N_RX_BUFS + N_TX_BUFS;

    constexpr int POLL_BATCH_SZ = 32;
    static_assert(POLL_BATCH_SZ >= EF_VI_EVENT_POLL_MIN_EVS, "event poll batch size too small\n");
    constexpr int REFILL_BATCH_SZ = 8;

    constexpr char INTERFACE_NAME[] = EFVI_TCP_INTERFACE;

    constexpr bool USE_CTPIO = EFVI_TCP_USE_CTPIO;

    constexpr int CTPIO_THRESH = 64;
}
