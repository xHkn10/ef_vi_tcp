#include "bench_config.h"
#include "tcp/socket.h"

#include <net/if.h>
#include <algorithm>


static int send_unordered(tcp::socket& sock, std::span<const std::byte> payload) {
    auto& ctx = sock.test_ctx();
    auto& tcb = sock.test_tcb();

    if (!sock.is_established() || payload.empty()) [[unlikely]]
        return 0;

    std::array<io::pkt_buf*, (MAX_SEND + TCP_MAX_PAYLOAD_SZ - 1) / TCP_MAX_PAYLOAD_SZ> batch{};
    int n_segs = 0;
    int staged = 0;

    while (staged < payload.size() && n_segs < batch.size() && !ctx.tx_free_stk.empty() && ctx.transmit_space() > n_segs) {
        if (const u32 in_flight = tcb.SND_NXT - tcb.SND_UNA; in_flight >= tcb.SND_WND)
            break;

        const int id = pop_back(ctx.tx_free_stk);
        io::pkt_buf* pb = ctx.tx_pkt_bufs[id];

        const int chunk = std::min<int>(TCP_MAX_PAYLOAD_SZ, payload.size() - staged);
        std::memcpy(pb->dma_buf + TCP_TOTAL_HDR_SZ, payload.data() + staged, chunk);
        pb->set_payload_sz(chunk);

        net::get_ip_hdr(pb)->len = to_net<u16>(IP_HDR_SZ + TCP_HDR_SZ + chunk);

        net::tcp_hdr* tcp = net::get_tcp_hdr(pb);
        tcp->control = ACK_FLAG;
        tcp->doffset_reserved = TCP_DEFAULT_DOFFSET_RESERVED;
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
    sock.bind(enp1s0f0_ip, enp1s0f0_port, enp1s0f1_ip, enp1s0f1_port, enp1s0f1_mac, enp1s0f0_mac);
    sock.connect();

    while (!sock.is_established())
        sock.poll();

    std::puts("Unordered send peer connected\n");

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
            const auto amount = std::uniform_int_distribution{1u, std::min<u32>(left, MAX_SEND)}(gen);
            std::span bytes_span = std::span{bytes}.first(amount);
            while (!bytes_span.empty()) {
                const auto actual_sent = send_unordered(sock, bytes_span);
                left -= actual_sent;
                bytes_span = bytes_span.subspan(actual_sent);
                sock.poll();
            }
        }
        while (!sock.tx_flushed())
            sock.poll();
    }
    const auto t1 = io::cycle_timer::now();

    const double bench_ms = static_cast<double>(t1 - t0) / io::cycle_timer::cycles_per_ms;

    std::printf("Unordered byte send took %f ms\n", bench_ms);

    constexpr auto n_gigabit = (double)N_BYTES / (1024 * 1024 * 1024) * 8;
    constexpr auto total_sent = n_gigabit * (78 + TCP_MAX_PAYLOAD_SZ) / TCP_MAX_PAYLOAD_SZ;

    const auto throughput = total_sent / (bench_ms / 1000);
    const auto goodput = n_gigabit / (bench_ms / 1000);

    std::printf("Throughput is %f Gbps\n", throughput);
    std::printf("Goodput is %f Gbps\n", goodput);
}

// CTPIO underrun bug
