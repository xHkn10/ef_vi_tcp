// ---------------------------------------------------------------------------
// Adversarial peer
// Connects to the BIST simulator and runs one hostile scenario,
// chosen by argv[1] (default: DEFAULT_SCENARIO).
//
// run the borsa istanbul emulator in one process,
// sudo ip netns exec echons ./binary in [scenario] in another
//
// A single Account bundles the socket + Session + Application
// we drive the TCP handshake here, then the scenario owns the SoupBin lifecycle (see scenarios.h).
// ---------------------------------------------------------------------------

#include "account/Account.h"
#include "adversary_config.h"
#include "scenarios.h"

#include "io/cycle_timer.h"

#include <net/if.h>

#include <cstdio>
#include <memory>
#include <string_view>

static_assert(!ENABLE_PASSIVE_OPEN);

static void list_scenarios() {
    std::puts("available scenarios:");
    for (const auto& s : adv::SCENARIOS)
        std::printf("  %-20.*s  %.*s\n",
                    (int)s.name.size(), s.name.data(),
                    (int)s.desc.size(), s.desc.data());
}

int main(int argc, char** argv) {
    if (if_nametoindex(io::INTERFACE_NAME) == 0) {
        LOG_ERROR("Failed to find interface %s", io::INTERFACE_NAME);
        return 1;
    }

    const auto want = argc > 1 ? std::string_view{argv[1]} : DEFAULT_SCENARIO;
    const adv::Scenario* chosen = adv::find_scenario(want);
    if (!chosen) {
        LOG_ERROR("unknown scenario '%.*s'", static_cast<int>(want.size()), want.data());
        list_scenarios();
        return 1;
    }

    // Account owns tcp::socket (opens the NIC in its ctor) -> heap-allocate.
    const auto acc = std::make_unique<Account>();
    acc->cfg = {
        .smac = us_mac,
        .dmac = them_mac,
        .local_ip = us_ip,
        .remote_ip = them_ip,
        .local_port = us_port,
        .remote_port = them_port,
        .username = username,
        .pass = pass,
    };

    if (!acc->sock.bind(acc->cfg.local_ip, acc->cfg.local_port,
                        acc->cfg.remote_ip, acc->cfg.remote_port,
                        acc->cfg.dmac, acc->cfg.smac)) {
        LOG_ERROR("bind failed");
        return 1;
    }

    LOG_INFO("connecting to simulator...");
    acc->sock.connect();
    const u64 deadline = io::cycle_timer::now() + 5000 * io::cycle_timer::cycles_per_ms;
    while (!acc->sock.is_established() && io::cycle_timer::now() < deadline) {
        acc->sock.poll();
        if (acc->sock.is_closed())
            acc->sock.connect();
    }
    if (!acc->sock.is_established()) {
        LOG_ERROR("connect failed (no ESTABLISHED within 5s)");
        return 1;
    }

    LOG_INFO("connected. running scenario: %.*s", static_cast<int>(chosen->name.size()), chosen->name.data());

    chosen->fn(acc->sock, acc->session, acc->app);

    LOG_INFO("scenario complete, closing");

    acc->sock.close();
    for (int i = 0; i < 100000; ++i)
        acc->sock.poll();

    return 0;
}
