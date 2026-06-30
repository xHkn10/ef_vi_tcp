#include "OuchApplication.h"

void OuchApplication::on_disconnect() {

}

void OuchApplication::on_login_rejected(char reject_reason_code) {

}

void OuchApplication::on_message(char msg_type, std::span<std::byte> payload) {

}

void OuchApplication::on_login_accepted(std::span<std::byte> payload) {

}
