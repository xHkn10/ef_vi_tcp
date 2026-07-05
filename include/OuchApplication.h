#pragma once

#include <span>
#include <string_view>
#include "types.h"

class SoupBinTcpSession;

class OuchApplication {
public:
    bool enter_order(std::string_view token, u32 book_id, i32 price, u8 tif, u8 open_close, std::string_view account);
    bool cancel_order(std::string_view token);

    void on_message(char msg_type, std::span<std::byte> payload);

    void on_login_accepted(std::array<char, 10> session, u64 seq_num);
    void on_login_rejected(char reject_reason);

    void on_order_accepted();
    void on_order_rejected();
    void on_cancel_accepted();
    void on_cancel_rejected();

    void on_disconnect();

    void on_end_of_session();
};
