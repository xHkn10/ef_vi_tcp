#include <cstring>
#include <iostream>

#include "ouch/Application.h"
#include "soup/Session.h"

namespace ouch {
    bool Application::enter_order(std::string_view token, u32 book_id, char side, u64 quantity, i32 price, u8 tif, u8 open_close, std::string_view account) {
        if (!session || !session->is_logged_in()) {
            LOG_ERROR("Not logged into a session");
            return false;
        }
        if (token.size() > sizeof(enter_order_msg::order_token)) {
            LOG_ERROR("Token size too big, cannot be %lu bytes", token.size());
            return false;
        }
        if (account.size() > sizeof(enter_order_msg::client_account)) {
            LOG_ERROR("Client account size too big, cannot be %lu bytes", account.size());
            return false;
        }

        enter_order_msg order{
            ENTER_ORDER_VAL,
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
        std::memset(order.order_token + token.size(), ' ', sizeof(enter_order_msg::order_token) - token.size());

        std::memcpy(order.client_account, account.data(), account.size());
        std::memset(order.client_account + account.size(), ' ', sizeof(enter_order_msg::client_account) - account.size());

        return session->send_unsequenced(std::as_bytes(std::span{&order, 1}));
    }

    bool Application::cancel_order(const std::string_view token) {
        if (!session || !session->is_logged_in()) {
            LOG_ERROR("Not logged into a session");
            return false;
        }
        if (token.size() > sizeof(cancel_order_msg::order_token)) {
            LOG_ERROR("Token size too big, cannot be %lu bytes", token.size());
            return false;
        }

        cancel_order_msg cancel{
            CANCEL_ORDER_VAL,
            {}
        };

        std::memcpy(cancel.order_token, token.data(), token.size());
        std::memset(cancel.order_token + token.size(), ' ', sizeof(cancel_order_msg::order_token) - token.size());

        return session->send_unsequenced(std::as_bytes(std::span{&cancel, 1}));
    }

    // OuchApplication::on_message's span includes the OUCH msg type char
    void Application::on_message(std::span<std::byte> ouch_msg) {
        switch (const char msg_type = static_cast<char>(ouch_msg[0])) {
            case ORDER_ACCEPTED_VAL: {
                if (ouch_msg.size() != sizeof(order_accepted_msg)) {
                    LOG_ERROR("Expected %lu bytes, got %lu bytes", sizeof(order_accepted_msg), ouch_msg.size());
                    break;
                }
                on_order_accepted(*reinterpret_cast<order_accepted_msg*>(ouch_msg.data())); // dc if this is UB
                break;
            }
            case ORDER_EXECUTED_VAL: {
                if (ouch_msg.size() != sizeof(order_executed_msg)) {
                    LOG_ERROR("Expected %lu bytes, got %lu bytes", sizeof(order_executed_msg), ouch_msg.size());
                    break;
                }
                on_order_executed(*reinterpret_cast<order_executed_msg*>(ouch_msg.data()));
                break;
            }
            case ORDER_REJECTED_VAL: {
                if (ouch_msg.size() != sizeof(order_rejected_msg)) {
                    LOG_ERROR("Expected %lu bytes, got %lu bytes", sizeof(order_rejected_msg), ouch_msg.size());
                    break;
                }
                on_order_rejected(*reinterpret_cast<order_rejected_msg*>(ouch_msg.data()));
                break;
            }
            case CANCEL_ACCEPTED_VAL: {
                if (ouch_msg.size() != sizeof(cancel_accepted_msg)) {
                    LOG_ERROR("Expected %lu bytes, got %lu bytes", sizeof(cancel_accepted_msg), ouch_msg.size());
                    break;
                }
                on_cancel_accepted(*reinterpret_cast<cancel_accepted_msg*>(ouch_msg.data()));
                break;
            }
            default:
                LOG_WARN("Unknown ouch msg type: %c", msg_type);
        }
    }

    void Application::on_order_accepted(const order_accepted_msg& msg) {
        LOG_INFO("Order accepted");
    }

    void Application::on_order_rejected(const order_rejected_msg& msg) {
        LOG_INFO("Order rejected");
    }

    void Application::on_order_executed(const order_executed_msg& msg) {
        LOG_INFO("Order executed");
    }

    void Application::on_cancel_accepted(const cancel_accepted_msg& msg) {
        LOG_INFO("Cancel accepted");
    }

    void Application::on_disconnect() {
        LOG_INFO("Session disconnected");
    }

    void Application::on_login_rejected(char reject_reason_code) {
        LOG_INFO("Login rejected with reject code %c", reject_reason_code);
    }

    void Application::on_login_accepted(u64 seq_num) {
        LOG_INFO("Login accepted, sequence number is %lu", seq_num);
    }

    void Application::on_end_of_session() {
        LOG_INFO("End of session");
    }

    void Application::attach(soup::Session& s) {
        session = &s;
    }
}
