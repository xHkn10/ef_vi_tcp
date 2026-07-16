#pragma once

#include <span>
#include <string_view>

#include "ouch_messages.h"
#include "types.h"

namespace soup {
    class Session;
}

namespace ouch {
    class Application {
    friend soup::Session;
    public:
        bool enter_order(std::string_view token, u32 book_id, char side, u64 quantity, i32 price, u8 tif, u8 open_close, std::string_view account);
        bool cancel_order(std::string_view token);

        void on_message(std::span<std::byte> ouch_msg);

        void on_login_accepted(u64 seq_num);
        void on_login_rejected(char reject_reason);

        void on_order_accepted(const order_accepted_msg& msg);
        void on_order_rejected(const order_rejected_msg& msg);
        void on_order_executed(const order_executed_msg& msg);
        void on_cancel_accepted(const cancel_accepted_msg& msg);

        void on_disconnect();

        void on_end_of_session();

    private:
        void attach(soup::Session& s);
        soup::Session* session = nullptr;
    };
}
