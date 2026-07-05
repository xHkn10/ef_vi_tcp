#pragma once

#include <span>
#include <string_view>

#include "ouch_messages.h"
#include "types.h"

class SoupBinTcpSession;

class OuchApplication {
friend SoupBinTcpSession;
public:
    bool enter_order(std::string_view token, u32 book_id, char side, u64 quantity, i32 price, u8 tif, u8 open_close, std::string_view account);
    bool cancel_order(std::string_view token);

    void on_message(std::span<std::byte> ouch_msg);

    void on_login_accepted(std::array<char, 10> session, u64 seq_num);
    void on_login_rejected(char reject_reason);

    void on_order_accepted(const ouch_order_accepted& msg);
    void on_order_rejected(const ouch_order_rejected& msg);
    void on_order_executed(const ouch_order_executed& msg);
    void on_cancel_accepted(const ouch_cancel_accepted& msg);

    void on_disconnect();

    void on_end_of_session();

private:
    void attach(SoupBinTcpSession& s);
    SoupBinTcpSession* session = nullptr;
};
