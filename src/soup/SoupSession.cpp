#include "soup/SoupSession.h"

#include <netinet/in.h>

namespace soup {
    SoupSession::SoupSession(tcp::socket &sock, ouch::OuchApplication &app) : sock{sock}, app{app} {
        app.attach(*this);
    }

    bool SoupSession::login(std::string_view username, std::string_view password, std::string_view session, std::string_view seq) {
        if (username.size() > 6) {
            LOG_ERROR("Username cannot be longer than 6 bytes");
            return false;
        }
        if (password.size() > 10) {
            LOG_ERROR("Password cannot be longer than 10 bytes");
            return false;
        }
        if (session.size() > 10) {
            LOG_ERROR("Session cannot be longer than 10 bytes");
            return false;
        }
        if (seq.size() > 20) {
            LOG_ERROR("Sequence cannot be longer than 20 bytes");
            return false;
        }

        std::array<std::byte, 49> packet{};

        auto write_left_justified = [&packet](int offset, int max_len, auto& vw) {
            std::memcpy(&packet[offset], vw.data(), vw.size());
            std::memset(&packet[offset] + vw.size(), ' ', max_len - vw.size());
        };
        auto write_right_justified = [&packet](int offset, int max_len, auto& vw) {
            std::memset(&packet[offset], ' ', max_len - vw.size());
            std::memcpy(&packet[offset] + max_len - vw.size(), vw.data(), vw.size());
        };

        *reinterpret_cast<u16*>(packet.data()) = to_net<u16>(47);
        packet[2] = static_cast<std::byte>('L');

        write_left_justified(3, 6, username);
        write_left_justified(9, 10, password);
        write_right_justified(19, 10, session);
        write_right_justified(29, 20, seq);

        if (sock.send(packet)) {
            state = SessionState::LoggingIn;
            last_tx_cycles = last_rx_cycles = io::cycle_timer::now();
            return true;
        }

        return false;
    }

    bool SoupSession::logout() {
        std::array<std::byte, 3> logout_packet{};
        *reinterpret_cast<u16*>(logout_packet.data()) = to_net<u16>(1);
        logout_packet[2] = static_cast<std::byte>('O');

        if (sock.send(logout_packet)) {
            state = SessionState::Disconnected;
            return true;
        }

        return false;
    }

    void SoupSession::handle_rx() {
        const io::rx_sgl sgl = sock.receive_available();

        auto* cur_buf = sgl.head;

        if (!cur_buf)
            return;

        last_rx_cycles = io::cycle_timer::now();

        auto* cur_ptr = cur_buf->meta.payload.data();
        u32 offset = 0;
        u32 consumed_bytes = 0;

        auto advance = [&](const u32 steps) {
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
        auto fragment_aware_advance = [&](u32 n_bytes) {
            while (n_bytes > 0) {
                if (!cur_buf) [[unlikely]] {
                    LOG_DEBUG("cur_buf shouldn't have been null");
                    break;
                }
                u32 chunk_sz = std::min<u32>(n_bytes, cur_buf->meta.payload.size() - offset);
                n_bytes -= chunk_sz;
                advance(chunk_sz);
            }
        };
        auto fragmented_memcpy = [&](std::byte* dst, u32 n_bytes) {
            u32 dst_offset = 0;
            while (n_bytes > 0) {
                const u32 chunk_sz = std::min<u32>(n_bytes, cur_buf->meta.payload.size() - offset);
                std::memcpy(dst + dst_offset, cur_ptr + offset, chunk_sz);

                n_bytes -= chunk_sz;
                dst_offset += chunk_sz;

                advance(chunk_sz);
            }
        };

        const int available_bytes = sgl.n_bytes;
        while (available_bytes - consumed_bytes >= 3) {
            u32 soup_len = 0; // 16 bits actually

            soup_len |= static_cast<u32>(*(cur_ptr + offset)) << 8;
            advance(1);

            soup_len |= static_cast<u32>(*(cur_ptr + offset));
            advance(1);

            const char soup_msg_type = static_cast<char>(*(cur_ptr + offset));
            advance(1);

            if (soup_len == 0) [[unlikely]] {
                LOG_ERROR("0 length soupbintcp message received. Aborting...");
                goto fatal;
            }

            if (available_bytes - consumed_bytes < soup_len - 1) {
                consumed_bytes -= 3;
                break;
            }

            switch (soup_msg_type) {
                case LOGIN_ACCEPTED: {
                    if (soup_len != 31) {
                        LOG_ERROR("Expected soupbintcp len 31 for LOGIN ACCEPTED, received %u. Aborting...", soup_len);
                        goto fatal;
                    }

                    state = SessionState::Active;
                    fragmented_memcpy(reinterpret_cast<std::byte*>(session.data()), 10);
                    int skipped_bytes = 0;
                    while (skipped_bytes < 20 && static_cast<char>(*(cur_ptr + offset)) == ' ') {
                        ++skipped_bytes;
                        advance(1);
                    }
                    char seq_num_bytes[21]{};
                    fragmented_memcpy(reinterpret_cast<std::byte*>(seq_num_bytes), 20 - skipped_bytes);
                    seq_num = strtoll(seq_num_bytes, nullptr, 10);
                    app.on_login_accepted(session, seq_num);
                    break;
                }
                case LOGIN_REJECTED: {
                    if (soup_len != 2) {
                        LOG_ERROR("Expected soupbintcp len 2 for LOGIN REJECTED, received %u. Aborting...", soup_len);
                        goto fatal;
                    }

                    state = SessionState::Disconnected;
                    const char rej_reason = static_cast<char>(*(cur_ptr + offset));
                    advance(1);
                    LOG_INFO("Reject reason: %c", rej_reason);

                    app.on_login_rejected(rej_reason);
                    break;
                }
                case HEARTBEAT: {
                    if (soup_len != 1) {
                        LOG_ERROR("Expected soupbintcp len 1 for HEARTBEAT, received %u. Aborting...", soup_len);
                        goto fatal;
                    }
                    break;
                }
                case END_OF_SESSION: {
                    if (soup_len != 1) {
                        LOG_ERROR("Expected soupbintcp len 1 for END OF SESSION, received %u. Aborting...", soup_len);
                        goto fatal;
                    }
                    state = SessionState::Disconnected;
                    app.on_end_of_session();
                    break;
                }
                case SEQUENCED_DATA: [[likely]] {
                    if (soup_len < 2) [[unlikely]] {
                        LOG_ERROR("soup_len cannot be %u bytes. Aborting...", soup_len);
                        goto fatal;
                    }

                    const u32 ouch_len = soup_len - 1;
                    if (offset + ouch_len <= cur_buf->meta.payload.size()) {
                        app.on_message({cur_ptr + offset, ouch_len});
                        advance(ouch_len);
                    } else {
                        fragmented_memcpy(fragment_buffer, ouch_len);
                        app.on_message({fragment_buffer, ouch_len});
                    }

                    ++seq_num;
                    break;
                }
                default: {
                    LOG_WARN("Unknown soupbintcp msg type");
                    fragment_aware_advance(soup_len - 1);
                    break;
                }
            }
        }

        sock.consume(sgl, static_cast<int>(consumed_bytes));
        return;

        fatal:
        sock.abort();
        sock.consume(sgl, sgl.n_bytes);
        state = SessionState::Disconnected;
        app.on_disconnect();
    }

    bool SoupSession::send_unsequenced(std::span<const std::byte> ouch_payload) {
        if (ouch_payload.size() > ouch::MAX_OUCH_MSG_SZ) {
            LOG_ERROR("OUCH payload of size %lu bytes too big", ouch_payload.size());
            return false;
        }

        const u32 msg_len = ouch_payload.size() + 1;  // 16 bits actually
        const u32 total_len = msg_len + 2;
        std::array<std::byte, ouch::MAX_OUCH_MSG_SZ + 3> packet{};

        *reinterpret_cast<u16*>(packet.data()) = to_net<u16>(msg_len);
        packet[2] = static_cast<std::byte>(UNSEQUENCED_DATA);
        std::memcpy(packet.data() + 3, ouch_payload.data(), ouch_payload.size());

        if (sock.send(std::span{packet}.first(total_len))) {
            last_tx_cycles = io::cycle_timer::now();
            return true;
        }
        return false;
    }

    void SoupSession::poll() {
        handle_rx();
        check_timers();
    }

    void SoupSession::check_timers() {
        if (state != SessionState::Active && state != SessionState::LoggingIn)
            return;

        const u64 tx_elapsed_cycles = io::cycle_timer::now() - last_tx_cycles;
        if (tx_elapsed_cycles >= HEARTBEAT_MS * io::cycle_timer::cycles_per_ms)
            beat_heart();

        const u64 rx_elapsed_cycles = io::cycle_timer::now() - last_rx_cycles;
        if (rx_elapsed_cycles >= RX_TIMEOUT_MS * io::cycle_timer::cycles_per_ms) {
            LOG_ERROR("Server stopped responding. Connection lost.");
            state = SessionState::Disconnected;
            sock.abort();
            app.on_disconnect();
        }
    }

    bool SoupSession::beat_heart() {
        std::array<std::byte, 3> heartbeat{};
        *reinterpret_cast<u16*>(heartbeat.data()) = to_net<u16>(1);
        heartbeat[2] = static_cast<std::byte>('R');

        if (sock.send(heartbeat)) {
            last_tx_cycles = io::cycle_timer::now();
            return true;
        }
        return false;
    }

    bool SoupSession::is_logged_in() const {
        return state == SessionState::Active;
    }
}
