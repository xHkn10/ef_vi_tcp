#include "OuchApplication.h"

bool OuchApplication::enter_order(std::string_view token, u32 book_id, i32 price, u8 tif, u8 open_close, std::string_view account) {

}

bool OuchApplication::cancel_order(std::string_view token) {

}



void OuchApplication::on_disconnect() {

}

void OuchApplication::on_login_rejected(char reject_reason_code) {

}

void OuchApplication::on_message(char msg_type, std::span<std::byte> payload) {

}

void OuchApplication::on_login_accepted(std::array<char, 10> session, u64 seq_num) {

}

void OuchApplication::on_end_of_session() {

}
