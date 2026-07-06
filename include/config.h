#pragma once

#include <etherfabric/ef_vi.h>

#define DEBUG

constexpr int BUF_SZ = 2048;
constexpr int PAGE_SZ = 4096 * 1024;
constexpr int N_RX_BUFS = 1024;
constexpr int N_TX_BUFS = 1024;
constexpr int N_BUFS = N_RX_BUFS + N_TX_BUFS;

constexpr int POLL_BATCH_SZ = 32;
static_assert(POLL_BATCH_SZ >= EF_VI_EVENT_POLL_MIN_EVS, "event poll batch size too small\n");
constexpr int REFILL_BATCH_SZ = 8;
