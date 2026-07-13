#pragma once
#include <array>

#include "account/Account.h"

namespace engine {
    template <int N>
    struct Engine {
        std::array<Account, N> accounts;

        void run();
        void step(Account& acc);
    };
}
