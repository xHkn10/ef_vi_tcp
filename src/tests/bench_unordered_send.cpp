#include "tcp/socket.h"

#include <net/if.h>
#include <algorithm>
#include <random>

namespace {
    namespace {
        // 00:0f:53:a3:ea:40
        constexpr std::array<u8, 6> enp1s0f0_mac = {0x00, 0x0f, 0x53, 0xa3, 0xea, 0x40};
        constexpr u32 enp1s0f0_ip = (192u << 24) | (168u << 16) | (100u << 8) | (2u << 0);
        constexpr u16 local_port = 2069;
    }

    namespace {
        //00:0f:53:a3:ea:41
        constexpr std::array<u8, 6> enp1s0f1_mac = {0x00, 0x0f, 0x53, 0xa3, 0xea, 0x41};
        // 192.168.100.1/24
        constexpr u32 enp1s0f1_ip = (192u << 24) | (168u << 16) | (100u << 8) | (1u << 0);
        constexpr u16 remote_port = 2070;
    }

    template <typename T>
    T pop_back(std::vector<T>& vec) {
        T tmp = vec.back();
        vec.pop_back();
        return tmp;
    }
}

std::mt19937 gen(42);
std::mt19937 shuffle_gen(1337);

constexpr int N_BYTES = 100 * 1024 * 1024;
constexpr auto MAX_SEND = 10'000;

int send_unordered(tcp::socket& sock, std::span<const std::byte> payload) {
    auto& ctx = sock.test_ctx();
    auto& tcb = sock.test_tcb();

    if (!sock.is_established() || payload.empty()) [[unlikely]]
        return 0;

    std::array<io::pkt_buf*, MAX_SEND / TCP_MAX_PAYLOAD_SZ> batch;
    int n_segs = 0;
    int staged = 0;

    while (staged < payload.size() && n_segs < batch.size() && !ctx.tx_free_stk.empty() && ctx.transmit_space() > n_segs) {
        const int id = pop_back(ctx.tx_free_stk);
        io::pkt_buf* pb = ctx.tx_pkt_bufs[id];

        const int chunk = std::min(TCP_MAX_PAYLOAD_SZ, static_cast<int>(payload.size()) - staged);
        std::memcpy(pb->dma_buf + TCP_TOTAL_HDR_SZ, payload.data() + staged, chunk);
        pb->set_payload_sz(chunk);

        net::get_ip_hdr(pb)->len = to_net<u16>(IP_HDR_SZ + TCP_HDR_SZ + chunk);

        net::tcp_hdr* tcp = net::get_tcp_hdr(pb);
        tcp->control = ACK_FLAG;
        tcp->seq_num = to_net(tcb.SND_NXT);
        tcp->ack_num = to_net(tcb.RCV_NXT);

        pb->meta.tx_ref_cnt = 2;
        pb->meta.seq = tcb.SND_NXT;
        tcb.SND_NXT += chunk;

        tcb.tx_unacked.push_back(pb);
        if (tcb.tx_unacked.size() == 1)
            tcb.rto_deadline_cycles = io::cycle_timer::now() + io::cycle_timer::cycles_per_ms * RETRANSMISSION_TIMEOUT_MILLISECONDS;

        batch[n_segs++] = pb;
        staged += chunk;
    }

    if (n_segs == 0)
        return 0;

    tcb.need_ack = tcb.immediate_ack_req = false;

    std::shuffle(batch.begin(), batch.begin() + n_segs, shuffle_gen);

    for (int i = 0; i < n_segs; ++i)
        ctx.transmit(batch[i], TCP_TOTAL_HDR_SZ + static_cast<int>(batch[i]->meta.payload.size()));

    return staged;
}

int main() {
    if (if_nametoindex(io::INTERFACE_NAME) == 0) {
        LOG_ERROR("Failed to find interface %s", io::INTERFACE_NAME);
        return 1;
    }

    tcp::socket sock;
    sock.bind(enp1s0f0_ip, local_port, enp1s0f1_ip, remote_port, enp1s0f1_mac, enp1s0f0_mac);
    sock.connect();

    while (!sock.is_established())
        sock.poll();

    const std::vector<std::byte> bytes = [] {
        auto ret = std::vector<std::byte>(MAX_SEND);
        for (auto& b : ret)
            b = static_cast<std::byte>(rand());
        return ret;
    }();


    const auto t0 = io::cycle_timer::now();
    {
        auto left = N_BYTES;
        while (left) {
            const auto amount = std::uniform_int_distribution{1, std::min(left, MAX_SEND)}(gen);
            std::span bytes_span = std::span{bytes}.first(amount);
            while (!bytes_span.empty()) {
                const auto actual_sent = send_unordered(sock, bytes_span.first(amount));
                left -= actual_sent;
                bytes_span = bytes_span.subspan(actual_sent);
                sock.poll();
            }
        }
        while (!sock.test_tcb().tx_unacked.empty())
            sock.poll();
    }
    const auto t1 = io::cycle_timer::now();

    const double bench_ms = static_cast<double>(t1 - t0) / io::cycle_timer::cycles_per_ms;

    std::printf("Unordered byte send took %f ms\n", bench_ms);
}
