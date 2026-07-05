#include "OuchApplication.h"

#include "SoupBinTcpSession.h"

bool OuchApplication::enter_order(std::string_view token, u32 book_id, char side, u64 quantity, i32 price, u8 tif, u8 open_close, std::string_view account) {
    if (!session || !session->is_logged_in())
        return false;

    return true;
}

bool OuchApplication::cancel_order(std::string_view token) {
    if (!session || !session->is_logged_in())
        return false;
    return true;
}

// OuchApplication::on_message also takes the OUCH msg type char
void OuchApplication::on_message(std::span<std::byte> ouch_msg) {
    char msg_type = static_cast<char>(ouch_msg[0]);

    switch (msg_type) {
        case ORDER_ACCEPTED: {

            break;
        }
        case ORDER_EXECUTED: {

            break;
        }
        case ORDER_REJECTED: {

            break;
        }
        case CANCEL_ACCEPTED: {

            break;
        }
        default:
            std::printf("Unkown ouch msg type: %c\n", msg_type);
    }
}

void OuchApplication::on_order_accepted(const ouch_order_accepted &msg) {

}

void OuchApplication::on_order_rejected(const ouch_order_rejected &msg) {

}

void OuchApplication::on_order_executed(const ouch_order_executed &msg) {

}

void OuchApplication::on_cancel_accepted(const ouch_cancel_accepted &msg) {

}

void OuchApplication::on_disconnect() {

}

void OuchApplication::on_login_rejected(char reject_reason_code) {

}


void OuchApplication::on_login_accepted(std::array<char, 10> session, u64 seq_num) {

}

void OuchApplication::on_end_of_session() {

}

void OuchApplication::attach(SoupBinTcpSession &s) {
    session = &s;
}
