#pragma once

#include "types.h"

struct ouch_enter_order {
    char msg_type; // 'O'
    char order_token[14];
    u32 order_book_id;
    char side; // 'B' buy, 'S' sell, 'T' short sell
    u64 quantity;
    i32 price;
    u8 time_in_force;           // 0 day, 3 immediate or cancel, 4 fill or kill
    u8 open_close;              // 0 default, 1 open, 2 close/net
    char client_account[16];
} __attribute__((packed));
static_assert(sizeof(ouch_enter_order) == 50);


struct ouch_cancel_order {
    char msg_type; // 'X'
    char order_token[14];
} __attribute__((packed));
static_assert(sizeof(ouch_cancel_order) == 15);

struct ouch_order_accepted {
    char msg_type; // 'A'
    u64 timestamp;              // UNIX time in nanoseconds
    char order_token[14];
    u32 order_book_id;
    char side;
    u64 order_id;
    u64 quantity;               // quantity currently open in the book
    i32 price;
    u8 time_in_force;
    u8 open_close;
    char client_account[16];
    u8 order_state;             // 1 on book, 2 not on book, 98 paused
    char customer_info[15];
    char exchange_info[32];     // only first 16 bytes used
    u64 pre_trade_quantity;
    u64 display_quantity;
} __attribute__((packed));
static_assert(sizeof(ouch_order_accepted) == 130);

// covers rejected orders AND rejected cancels
struct ouch_order_rejected {
    char msg_type; // 'J'
    u64 timestamp;
    char order_token[14];
    i32 reject_code;
} __attribute__((packed));
static_assert(sizeof(ouch_order_rejected) == 27);

struct ouch_cancel_accepted {
    char msg_type; // 'C'
    u64 timestamp;
    char order_token[14];
    u32 order_book_id;
    char side;
    u64 order_id;
    u8 reason; // 1 canceled by user, 9 canceled by system
} __attribute__((packed));
static_assert(sizeof(ouch_cancel_accepted) == 37);

struct ouch_order_executed {
    char msg_type;              // 'E'
    u64 timestamp;              // UNIX time in nanoseconds
    char order_token[14];
    u32 order_book_id;          // one message per leg for combination fills
    u64 traded_quantity;        // amount filled in THIS transaction
    i32 trade_price;            // signed, implied decimals per ITCH directory
    u8 match_id[12];            // big-endian 12-byte numeric, unique per trade
    u8 client_category;         // 1 client, 2 house, 7 fund, ... (unused on VIOP)
    char reserved[16];
} __attribute__((packed));
static_assert(sizeof(ouch_order_executed) == 68);
