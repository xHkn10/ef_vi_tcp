#pragma once

#include <array>

#include "account/Account.h"

namespace engine {
    template <int N>
    struct Engine {
        std::array<Account, N> accounts;

        void run() {
            while (true) {
                for (auto& acc : accounts) {
                    acc.sock.poll();
                    step(acc);
                }
            }
        }

        static void step(Account& acc) {
            switch (acc.phase) {
                case Phase::Idle: {
                    if (acc.sock.connect())
                        acc.phase = Phase::Connecting;
                    break;
                }
                case Phase::Connecting: {
                    if (acc.sock.is_closed())
                        acc.phase = Phase::Idle;
                    else if (!acc.sock.is_established())
                        break;
                    else if (acc.session.login(acc.cfg.username, acc.cfg.pass))
                        acc.phase = Phase::LoggingIn;
                    break;
                }
                case Phase::LoggingIn: {
                    acc.session.poll();
                    if (acc.session.is_logged_in())
                        acc.phase = Phase::Active;
                    else if (acc.session.is_disconnected() || !acc.sock.is_established())
                        teardown(acc);
                    break;
                }
                case Phase::Active: [[likely]] {
                    acc.session.poll();
                    if (acc.session.is_logged_out()) [[unlikely]]
                        acc.phase = Phase::Stopped;
                    else if (acc.session.is_disconnected() || !acc.sock.is_established()) [[unlikely]]
                        teardown(acc);
                    break;
                }
                case Phase::Stopped:
                    break;
            }
        }
    private:
        static void teardown(Account& acc) {
            acc.sock.abort();
            acc.session.reset();
            acc.phase = Phase::Idle;
        }
    };
}
