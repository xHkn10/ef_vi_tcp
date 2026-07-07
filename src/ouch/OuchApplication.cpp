#include "ouch/OuchApplication.h"
#include "soup/SoupSession.h"

#include <iostream>

namespace ouch {
    bool OuchApplication::enter_order(std::string_view token, u32 book_id, char side, u64 quantity, i32 price, u8 tif, u8 open_close, std::string_view account) {
        if (!session || !session->is_logged_in()) {
            LOG_ERROR("Not logged into a session");
            return false;
        }
        if (token.size() > sizeof(ouch_enter_order::order_token)) {
            LOG_ERROR("Token size too big, cannot be %lu bytes", token.size());
            return false;
        }
        if (account.size() > sizeof(ouch_enter_order::client_account)) {
            LOG_ERROR("Client account size too big, cannot be %lu bytes", account.size());
            return false;
        }

        ouch_enter_order order{
            ENTER_ORDER,
            {},
            to_net(book_id),
            side,
            to_net(quantity),
            static_cast<i32>(to_net(static_cast<u32>(price))),
            tif,
            open_close,
            {}
        };

        std::memcpy(order.order_token, token.data(), token.size());
        std::memset(order.order_token + token.size(), ' ', sizeof(ouch_enter_order::order_token) - token.size());

        std::memcpy(order.client_account, account.data(), account.size());
        std::memset(order.client_account + account.size(), ' ', sizeof(ouch_enter_order::client_account) - account.size());

        return session->send_unsequenced(std::as_bytes(std::span{&order, 1}));
    }

    bool OuchApplication::cancel_order(const std::string_view token) {
        if (!session || !session->is_logged_in()) {
            LOG_ERROR("Not logged into a session");
            return false;
        }
        if (token.size() > sizeof(ouch_cancel_order::order_token)) {
            LOG_ERROR("Token size too big, cannot be %lu bytes", token.size());
            return false;
        }

        ouch_cancel_order cancel{
            CANCEL_ORDER,
            {}
        };

        std::memcpy(cancel.order_token, token.data(), token.size());
        std::memset(cancel.order_token + token.size(), ' ', sizeof(ouch_cancel_order::order_token) - token.size());

        return session->send_unsequenced(std::as_bytes(std::span{&cancel, 1}));
    }

    // OuchApplication::on_message's span includes the OUCH msg type char
    void OuchApplication::on_message(std::span<std::byte> ouch_msg) {
        char msg_type = static_cast<char>(ouch_msg[0]);

        switch (msg_type) {
            case ORDER_ACCEPTED: {
                if (ouch_msg.size() != sizeof(ouch_order_accepted)) {
                    LOG_ERROR("Expected %lu bytes, got %lu bytes", sizeof(ouch_order_accepted), ouch_msg.size());
                    break;
                }
                on_order_accepted(*reinterpret_cast<ouch_order_accepted*>(ouch_msg.data())); // dc if this is UB
                break;
            }
            case ORDER_EXECUTED: {
                if (ouch_msg.size() != sizeof(ouch_order_executed)) {
                    LOG_ERROR("Expected %lu bytes, got %lu bytes", sizeof(ouch_order_executed), ouch_msg.size());
                    break;
                }
                on_order_executed(*reinterpret_cast<ouch_order_executed*>(ouch_msg.data()));
                break;
            }
            case ORDER_REJECTED: {
                if (ouch_msg.size() != sizeof(ouch_order_rejected)) {
                    LOG_ERROR("Expected %lu bytes, got %lu bytes", sizeof(ouch_order_rejected), ouch_msg.size());
                    break;
                }
                on_order_rejected(*reinterpret_cast<ouch_order_rejected*>(ouch_msg.data()));
                break;
            }
            case CANCEL_ACCEPTED: {
                if (ouch_msg.size() != sizeof(ouch_cancel_accepted)) {
                    LOG_ERROR("Expected %lu bytes, got %lu bytes", sizeof(ouch_cancel_accepted), ouch_msg.size());
                    break;
                }
                on_cancel_accepted(*reinterpret_cast<ouch_cancel_accepted*>(ouch_msg.data()));
                break;
            }
            default:
                LOG_WARN("Unknown ouch msg type: %c", msg_type);
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

    void OuchApplication::attach(soup::SoupSession &s) {
        session = &s;
    }
}
