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
            if (ouch_msg.size() != sizeof(ouch_order_accepted)) {
                std::printf("Expected %lu bytes, got %lu bytes.\n", sizeof(ouch_order_accepted), ouch_msg.size());
                break;
            }
            on_order_accepted(*reinterpret_cast<ouch_order_accepted*>(ouch_msg.data())); // dc if this is UB
            break;
        }
        case ORDER_EXECUTED: {
            if (ouch_msg.size() != sizeof(ouch_order_executed)) {
                std::printf("Expected %lu bytes, got %lu bytes.\n", sizeof(ouch_order_executed), ouch_msg.size());
                break;
            }
            on_order_executed(*reinterpret_cast<ouch_order_executed*>(ouch_msg.data()));
            break;
        }
        case ORDER_REJECTED: {
            if (ouch_msg.size() != sizeof(ouch_order_rejected)) {
                std::printf("Expected %lu bytes, got %lu bytes.\n", sizeof(ouch_order_rejected), ouch_msg.size());
                break;
            }
            on_order_rejected(*reinterpret_cast<ouch_order_rejected*>(ouch_msg.data()));
            break;
        }
        case CANCEL_ACCEPTED: {
            if (ouch_msg.size() != sizeof(ouch_cancel_accepted)) {
                std::printf("Expected %lu bytes, got %lu bytes.\n", sizeof(ouch_cancel_accepted), ouch_msg.size());
                break;
            }
            on_cancel_accepted(*reinterpret_cast<ouch_cancel_accepted*>(ouch_msg.data()));
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
