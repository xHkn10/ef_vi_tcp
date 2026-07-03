#include "SoupBinTcpSession.h"

#include <netinet/in.h>

SoupBinTcpSession::SoupBinTcpSession(SfTcpSocket &sock, OuchApplication &app)
    : sock{sock}, app{app} {}

bool SoupBinTcpSession::login(std::string_view username, std::string_view password, std::string_view session, std::string_view seq) {
    if (username.size() > 6) {
        std::puts("Username cannot be longer than 6 bytes\n");
        return false;
    }
    if (password.size() > 10) {
        std::puts("Password cannot be longer than 10 bytes\n");
        return false;
    }
    if (session.size() > 10) {
        std::puts("Session cannot be longer than 10 bytes\n");
        return false;
    }
    if (seq.size() > 20) {
        std::puts("Sequence cannot be longer than 20 bytes\n");
        return false;
    }

    std::array<std::byte, 49> packet{};

    auto write_right_padded = [&packet](int offset, int max_len, auto& vw) {
        std::memcpy(&packet[offset] + max_len - vw.size(), vw.data(), vw.size());
        std::memset(&packet[offset], ' ', max_len - vw.size());
    };

    *reinterpret_cast<u16*>(packet.data()) = htons(47);
    packet[2] = static_cast<std::byte>('L');

    write_right_padded(3, 6, username);
    write_right_padded(9, 10, password);
    write_right_padded(19, 10, session);
    write_right_padded(29, 20, seq);

    if (sock.send(packet)) {
        state = SessionState::LoggingIn;
        last_tx_cycles = cycle_timer::now();
        return true;
    }

    return false;
}

bool SoupBinTcpSession::logout() {
    std::array<std::byte, 3> logout_packet{};
    *reinterpret_cast<u16*>(logout_packet.data()) = htons(1);
    logout_packet[2] = static_cast<std::byte>('O');

    if (sock.send(logout_packet)) {
        state = SessionState::Disconnected;
        return true;
    }

    return false;
}

void SoupBinTcpSession::handle_rx() {
    rx_sgl sgl = sock.receive_available();

    auto* cur_buf = sgl.head;

    if (!cur_buf)
        return;

    auto* cur_ptr = cur_buf->meta.payload.data();
    u32 offset = 0;
    u32 consumed_bytes = 0;

    auto advance = [&](u32 steps) {
        offset += steps;
        consumed_bytes += steps;

        // Currently, there can be NO empty segments
        // If there might be in the future, replace this with a while loop
        if (offset == cur_buf->meta.payload.size()) {
            offset = 0;
            cur_buf = cur_buf->meta.nxt;
            cur_ptr = cur_buf ? cur_buf->meta.payload.data() : nullptr;
        }
    };

    int available_bytes = sgl.n_bytes;
    while (available_bytes - consumed_bytes >= 3) {
        u32 msg_len = 0; // 16 bits actually

        msg_len |= static_cast<u32>(*(cur_ptr + offset)) << 8;
        advance(1);

        msg_len |= static_cast<u32>(*(cur_ptr + offset));
        advance(1);

        char msg_type = static_cast<char>(*(cur_ptr + offset));
        advance(1);

        if (available_bytes - consumed_bytes < msg_len - 1) {
            consumed_bytes -= 3;
            break;
        }

        int payload_len = static_cast<int>(msg_len) - 1;

        if (offset + payload_len <= cur_buf->meta.payload.size()) {
            app.on_message(msg_type, {cur_ptr + offset, msg_len - 1});
            advance(payload_len);
        } else {
            int bytes_remaining = payload_len;

            while (bytes_remaining > 0) {
                int chunk_sz = std::min(bytes_remaining, static_cast<int>(cur_buf->meta.payload.size() - offset));
                std::memcpy(fragment_buffer + fragment_buffer_sz, cur_ptr + offset, chunk_sz);

                bytes_remaining -= chunk_sz;
                fragment_buffer_sz += chunk_sz;

                advance(chunk_sz);
            }

            app.on_message(msg_type, {fragment_buffer, static_cast<u32>(fragment_buffer_sz)});
            fragment_buffer_sz = 0;
        }
    }

    if (consumed_bytes > 0)
        sock.consume(sgl, static_cast<int>(consumed_bytes));
}

bool SoupBinTcpSession::send_unsequenced(std::span<std::byte> ouch_payload) {
    u32 msg_len = ouch_payload.size() + 1;  // 16 bits actually
    u32 total_len = msg_len + 2;
    std::array<std::byte, 256> packet{};
    *reinterpret_cast<u16*>(packet.data()) = htons(msg_len);
    packet[2] = static_cast<std::byte>('U');
    std::memcpy(packet.data() + 3, ouch_payload.data(), ouch_payload.size());

    if (sock.send(std::span{packet}.first(total_len))) {
        last_tx_cycles = cycle_timer::now();
        return true;
    }
    return false;
}

void SoupBinTcpSession::poll() {
    handle_rx();
    check_timers();
}

void SoupBinTcpSession::check_timers() {
    if (state != SessionState::Active && state != SessionState::LoggingIn)
        return;

    u64 tx_elapsed_cycles = cycle_timer::now() - last_tx_cycles;
    if (tx_elapsed_cycles >= HEARTBEAT_MS * cycle_timer::cycles_per_ms)
        send_heartbeat();

    u64 rx_elapsed_cycles = cycle_timer::now() - last_rx_cycles;
    if (rx_elapsed_cycles >= RX_TIMEOUT_MS * cycle_timer::cycles_per_ms) {
        std::puts("[TIMEOUT] Server stopped responding. Connection lost.\n");
        state = SessionState::Disconnected;
        sock.abort();
        app.on_disconnect();
    }
}

bool SoupBinTcpSession::send_heartbeat() {
    std::array<std::byte, 3> heartbeat{};
    *reinterpret_cast<u16*>(heartbeat.data()) = htons(1);
    heartbeat[2] = static_cast<std::byte>('R');

    if (sock.send(heartbeat)) {
        last_tx_cycles = cycle_timer::now();
        return true;
    }
    return false;
}

bool SoupBinTcpSession::is_logged_in() const {
    return state == SessionState::Active;
}
