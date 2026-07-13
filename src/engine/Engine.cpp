#include "engine/Engine.h"

namespace engine {
    template <int N>
    void Engine<N>::run() {
        while (true) {
            for (auto& acc : accounts) {
                acc.sock.poll();
                step(acc);
            }
        }
    }

    template <int N>
    void Engine<N>::step(Account& acc) {
        switch (acc.phase) {
            case Phase::Idle: {
                if (!acc.sock.connect())
                    break;
                acc.phase = Phase::Connecting;
                break;
            }
            case Phase::Connecting: {
                if (!acc.session.login(acc.cfg.username, acc.cfg.pass, 0, 0))
                    break;
                acc.phase = Phase::LoggingIn;
                break;
            }
            case Phase::LoggingIn: {

                break;
            }
            case Phase::Active: [[likely]] {

                break;
            }
        }
    }
}
