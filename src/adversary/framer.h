#pragma once

// ---------------------------------------------------------------------------
// framer.h: pure byte builders + TCP-segment placement for the adversary
//
// Nothing here holds session state. The bytes go on the wire via the send
// helpers, which are the whole fragmentation lever
//
// SoupBinTCP frame on the wire: [u16 length][1 byte type][payload],
// ---------------------------------------------------------------------------


#include "tcp/socket.h"
#include "soup/Session.h"
#include "ouch/ouch_messages.h"
#include "types.h"

#include <array>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace adv {
    using bytes = std::vector<std::byte>;



    // ----------------------- BUFFER HELPERS -----------------------
    inline void put_u16(bytes& v, u16 x) {
        v.push_back(static_cast<std::byte>((x >> 8) & 0xff));
        v.push_back(static_cast<std::byte>(x & 0xff));
    }
    inline void put_bytes(bytes& v, std::span<const std::byte> s) {
        v.insert(v.end(), s.begin(), s.end());
    }
    // Append a well-formed SoupBinTCP frame
    inline void put_soup_frame(bytes& v, char type, std::span<const std::byte> payload) {
        put_u16(v, static_cast<u16>(payload.size() + 1));
        v.push_back(static_cast<std::byte>(type));
        put_bytes(v, payload);
    }
    // --------------------------------------------------------------





    // ----------------------- MESSAGE BUILDERS -----------------------
    inline bytes
    build_login(std::string_view user, std::string_view password, std::string_view session = "", std::string_view seq = "1") {
        std::array<std::byte, 46> p{};
        std::memset(p.data(), ' ', p.size());

        auto left = [&](int off, size_t max, std::string_view s) {
            std::memcpy(p.data() + off, s.data(), std::min(s.size(), max));
        };
        auto right = [&](int off, size_t max, std::string_view s) {
            const size_t n = std::min(s.size(), max);
            std::memcpy(p.data() + off + (max - n), s.data(), n);
        };

        left(0, 6, user);
        left(6, 10, password);
        right(16, 10, session);
        right(26, 20, seq);

        bytes v;
        put_soup_frame(v, 'L', p);
        return v;
    }
    // A well-formed OUCH enter_order, 50 bytes
    inline std::array<std::byte, sizeof(ouch::enter_order_msg)>
    build_enter_order(std::string_view token, u32 book_id, char side, u64 qty, i32 price,
                      u8 tif = 0, u8 open_close = 0, std::string_view account = "") {

        ouch::enter_order_msg o{};
        o.msg_type = ouch::ENTER_ORDER_VAL;

        std::memset(o.order_token, ' ', sizeof o.order_token);
        std::memcpy(o.order_token, token.data(), std::min(token.size(), sizeof o.order_token));

        o.order_book_id = to_net(book_id);
        o.side = side;
        o.quantity = to_net(qty);
        o.price = static_cast<i32>(to_net(static_cast<u32>(price)));
        o.time_in_force = tif;
        o.open_close = open_close;

        std::memset(o.client_account, ' ', sizeof o.client_account);
        std::memcpy(o.client_account, account.data(), std::min(account.size(), sizeof o.client_account));

        std::memset(o.customer_info, ' ', sizeof o.customer_info);
        std::memset(o.exchange_info, ' ', sizeof o.exchange_info);
        o.display_quantity = to_net(qty);

        std::array<std::byte, sizeof(o)> a{};
        std::memcpy(a.data(), &o, sizeof o);
        return a;
    }
    // wrap an OUCH payload in a soupbintcp unsequenced data ('U') frame.
    inline bytes build_unsequenced(std::span<const std::byte> ouch_payload) {
        bytes v;
        put_soup_frame(v, soup::UNSEQUENCED_DATA, ouch_payload);
        return v;
    }
    // A single well-formed order frame, handy as the "clean" baseline to mangle.
    inline bytes sample_order() {
        const auto o = build_enter_order("token", 70796, 'B', 100, 68100);
        return build_unsequenced(o);
    }
    // Client Heartbeat ('R'), 3 bytes.
    inline bytes build_client_heartbeat() {
        bytes v;
        put_soup_frame(v, 'R', {});
        return v;
    }
    // --------------------------------------------------------------





    // ----------------------- SEGMENT PLACEMENT -----------------------
    inline void send_seg(tcp::socket& sock, std::span<const std::byte> seg) {
        while (!seg.empty()) {
            const auto sent = sock.send(seg);
            if (sent == 0) sock.poll();
            seg = seg.subspan(sent);
        }
    }
    inline void frag_send(tcp::socket& sock, std::span<const std::byte> data, std::span<const int> chops) {
        int off = 0;
        for (int c : chops) {
            c = std::min(c, (int)data.size() - off);
            send_seg(sock, data.subspan(off, c));
            off += c;
        }
        if (off < data.size())
            send_seg(sock, data.subspan(off));
    }
    // One byte per TCP segment
    inline void dribble(tcp::socket& sock, std::span<const std::byte> data) {
        for (int i = 0; i < data.size(); ++i)
            send_seg(sock, data.subspan(i, 1));
    }
    inline void send_all(tcp::socket& sock, std::span<const std::byte> data) {
        int off = 0;
        while (off < data.size()) {
            const int n = sock.send(data.subspan(off));
            if (n == 0)
                sock.poll();
            else
                off += n;
        }
    }
    // --------------------------------------------------------------


}
