#pragma once

#include <span>

class OuchApplication {
public:
    void on_message(char msg_type, std::span<std::byte> payload);
    void on_login_accepted(std::span<std::byte> payload);
    void on_login_rejected(char reject_reason_code);
    void on_disconnect();
};
