#pragma once

// ---------------------------------------------------------------------------
// scenarios.h: the hostile-behavior catalog
//
// Each scenario runs from an already-ESTABLISHED TCP connection
// and is responsible for its own soupbintcp lifecycle.
// ---------------------------------------------------------------------------

#include "framer.h"
#include "adversary_config.h"

#include "io/cycle_timer.h"
#include "soup/Session.h"
#include "ouch/Application.h"
#include "tcp/socket.h"

#include <span>
#include <string_view>

namespace adv {

    using ScenarioFn = void (*)(tcp::socket&, soup::Session&, ouch::Application&);


    // --------------------------------- shared helpers ---------------------------------
    inline void drain(tcp::socket& sock, soup::Session& sess, u64 ms) {
        const u64 end = io::cycle_timer::now() + ms * io::cycle_timer::cycles_per_ms;
        while (io::cycle_timer::now() < end && !sock.is_closed()) {
            sock.poll();
            sess.poll();
        }
    }
    inline bool login_honest(tcp::socket& sock, soup::Session& sess, u64 timeout_ms = 3000) {
        sess.login(username, pass);
        const u64 end = io::cycle_timer::now() + timeout_ms * io::cycle_timer::cycles_per_ms;
        while (io::cycle_timer::now() < end && !sock.is_closed()) {
            sock.poll();
            sess.poll();
            if (sess.is_logged_in())
                return true;
            if (sess.is_disconnected())
                return false;
        }
        return sess.is_logged_in();
    }



    // --------------------------------- scenarios ---------------------------------

    // sit idle (send heartbeats, parse replies)
    inline void idle(tcp::socket& sock, soup::Session& sess, ouch::Application&) {
        if (!login_honest(sock, sess)) { LOG_ERROR("login failed"); return; }
        LOG_INFO("idle: logged in, heartbeating, watching the simulator for 60s");
        drain(sock, sess, 60000);
    }

    // split 2 byte soupbintcp length prf into 2 TCP segments
    inline void split_length_prefix(tcp::socket& sock, soup::Session& sess, ouch::Application&) {
        if (!login_honest(sock, sess)) { LOG_ERROR("login failed"); return; }
        const auto frame = sample_order();
        const int chops[] = {1};
        LOG_INFO("split_length_prefix: sending order in segments [1, %zu]", frame.size() - 1);
        frag_send(sock, frame, chops);
        drain(sock, sess, 3000);
    }

    // send 1 byte per TCP segment
    inline void byte_dribble(tcp::socket& sock, soup::Session& sess, ouch::Application&) {
        if (!login_honest(sock, sess)) { LOG_ERROR("login failed"); return; }
        const auto frame = sample_order();
        LOG_INFO("byte_dribble: sending %zu-byte order 1 byte/segment", frame.size());
        dribble(sock, frame);
        drain(sock, sess, 3000);
    }

    // coalesce many order frames into a single TCP segment
    inline void coalesce_burst(tcp::socket& sock, soup::Session& sess, ouch::Application&) {
        if (!login_honest(sock, sess)) { LOG_ERROR("login failed"); return; }
        bytes buf;
        constexpr int N = 20;                      // 20 * ~54B < snd_mss, so one segment
        for (int i = 0; i < N; ++i) {
            const auto f = sample_order();
            buf.insert(buf.end(), f.begin(), f.end());
        }
        LOG_INFO("coalesce_burst: %d orders in one %zu-byte segment", N, buf.size());
        send_seg(sock, buf);
        drain(sock, sess, 3000);
    }

    // flood the server with client heartbeats
    inline void heartbeat_flood(tcp::socket& sock, soup::Session& sess, ouch::Application&) {
        if (!login_honest(sock, sess)) { LOG_ERROR("login failed"); return; }
        const auto hb = build_client_heartbeat();
        constexpr int N = 5000;
        LOG_INFO("heartbeat_flood: %d client heartbeats as fast as possible", N);
        for (int i = 0; i < N; ++i) {
            send_seg(sock, hb);
            if ((i & 0xFF) == 0) sock.poll();
        }
        drain(sock, sess, 3000);
    }

    inline void random_frag(tcp::socket& sock, soup::Session& sess, ouch::Application&) {
        if (!login_honest(sock, sess)) { LOG_ERROR("login failed"); return; }
        LOG_INFO("random_frag: 10 orders, random segment boundaries");
        for (int rep = 0; rep < 10; ++rep) {
            const auto frame = sample_order();
            std::span spn{frame};
            while (!spn.empty()) {
                const int n = std::uniform_int_distribution{1, static_cast<int>(std::min<size_t>(spn.size(), 5))}(gen);
                send_seg(sock, spn.first(n));
                spn = spn.subspan(n);
                sock.poll();
                sess.poll();
            }
        }
        drain(sock, sess, 3000);
    }





    // ------------------------------ registry ------------------------------
    struct Scenario {
        std::string_view name;
        ScenarioFn fn;
        std::string_view desc;
    };

    inline const Scenario SCENARIOS[] = {
        {.name = "idle",                .fn = idle,                .desc = "honest baseline: log in and sit idle"},
        {.name = "split_length_prefix", .fn = split_length_prefix, .desc = "order with its length prefix split across 2 segments"},
        {.name = "byte_dribble",        .fn = byte_dribble,        .desc = "order sent one byte per TCP segment"},
        {.name = "coalesce_burst",      .fn = coalesce_burst,      .desc = "many order frames coalesced into one segment"},
        {.name = "heartbeat_flood",     .fn = heartbeat_flood,     .desc = "flood the server with client heartbeats"},
        {.name = "random_frag",         .fn = random_frag,         .desc = "orders fragmented at random boundaries"},
    };

    inline const Scenario* find_scenario(std::string_view name) {
        for (const auto& s : SCENARIOS)
            if (s.name == name)
                return &s;
        return nullptr;
    }
}
