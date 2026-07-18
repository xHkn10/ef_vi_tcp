#pragma once

#include "types.h"

constexpr int PKT_BUF_MD_SZ = 64;

constexpr int ETH_HDR_SZ = 14;
constexpr int IP_HDR_SZ = 20;
constexpr int TCP_HDR_SZ = 20;
constexpr int TCP_TOTAL_HDR_SZ = ETH_HDR_SZ + IP_HDR_SZ + TCP_HDR_SZ;

constexpr int TCP_TOTAL_METADATA_SZ = TCP_TOTAL_HDR_SZ + PKT_BUF_MD_SZ;
constexpr int TCP_MAX_PAYLOAD_SZ = io::BUF_SZ - TCP_TOTAL_METADATA_SZ - 64;

constexpr u32 SYN_FLAG = 0b00000010;
constexpr u32 ACK_FLAG = 0b00010000;
constexpr u32 FIN_FLAG = 0b00000001;
constexpr u32 RST_FLAG = 0b00000100;

constexpr int IP_S_ADDR_OFFSET = 16;
constexpr int IP_D_ADDR_OFFSET = 20;
constexpr int TCP_SEQ_NUM_OFFSET = 32;
constexpr int TCP_ACK_NUM_OFFSET = 36;

#ifdef TCP_TEST_HOOKS
    // so we don't wait forever in tests
    constexpr int RETRANSMISSION_TIMEOUT_MILLISECONDS = 1;
    constexpr int CONNECT_TIMEOUT_MILLISECONDS = 50;
    constexpr int DELAYED_ACK_TIMEOUT_MILLISECONDS = 5;
    constexpr int TIME_WAIT_MILLISECONDS = 100;
#else
    constexpr int RETRANSMISSION_TIMEOUT_MILLISECONDS = 20;
    constexpr int CONNECT_TIMEOUT_MILLISECONDS = 1000;
    constexpr int DELAYED_ACK_TIMEOUT_MILLISECONDS = 100;
    constexpr int TIME_WAIT_MILLISECONDS = 60'000; // linux is also 60s
#endif


namespace net {
    // FIELDS ARE IN NETWORK ORDER

    // Because the headers start from the beginning of a cache line, (dma_buf is aligned)
    // and the size of headers (with no options) is 14 + 20 + 20 = 54,
    // unpadded headers will never trigger an unaligned access penalty causing from line splits.

    // 14 bytes
    struct eth_hdr {
        std::array<u8, 6> dmac;
        std::array<u8, 6> smac;
        u16 ethertype; // ASSUMING ALWAYS 0x0800 (ipv4)
    } __attribute__((packed));

    // 20 bytes (no options)
    struct ip_hdr {
        u8 version_ihl;
        u8 tos;
        u16 len;
        u16 id;
        u16 flag_frag_offset; // first 3 bits is flag, other 13 bits is frag offset;
        u8 ttl;
        u8 proto;
        u16 csum;
        u32 s_addr;
        u32 d_addr;
        // options will follow d_addr from here, if there are any
        // ASSUMING NO IP OPTIONS
    } __attribute__((packed));

    // 20 bytes (no options)
    struct tcp_hdr {
        u16 src_port;
        u16 dst_port;
        u32 seq_num;
        u32 ack_num;
        u8 doffset_reserved; // first 4 bits doffset, then reserved
        u8 control; // CWR, ECE, URG, ACK, PSH, RST, SYN, FIN (no NS)
        u16 window;
        u16 checksum;
        u16 urgent_ptr;
        // tcp options follow this, of size max 40 bytes
    } __attribute__((packed));

    inline eth_hdr* get_eth_hdr(io::pkt_buf* pb) {
        return reinterpret_cast<eth_hdr*>(pb->dma_buf);
    }
    inline ip_hdr* get_ip_hdr(io::pkt_buf* pb) {
        return reinterpret_cast<ip_hdr*>(pb->dma_buf + ETH_HDR_SZ);
    }
    inline tcp_hdr* get_tcp_hdr(io::pkt_buf* pb) {
        return reinterpret_cast<tcp_hdr*>(pb->dma_buf + ETH_HDR_SZ + IP_HDR_SZ);
    }

    inline std::span<std::byte> get_tcp_payload(io::pkt_buf* pb) {
        const auto* ip = get_ip_hdr(pb);
        auto* tcp = get_tcp_hdr(pb);

        const int tcp_header_len = (tcp->doffset_reserved >> 4) * 4;
        const int payload_sz = from_net(ip->len) - IP_HDR_SZ - tcp_header_len;

        if (payload_sz < 0 || payload_sz > TCP_MAX_PAYLOAD_SZ) [[unlikely]]
            return {};

        return {reinterpret_cast<std::byte*>(tcp) + tcp_header_len, static_cast<u64>(payload_sz)};
    }

    inline std::span<std::byte> get_tcp_options(io::pkt_buf* pb) {
        auto* tcp = get_tcp_hdr(pb);
        u32 tcp_header_len = (tcp->doffset_reserved >> 4) * 4;
        u32 tcp_options_len = tcp_header_len - TCP_HDR_SZ;
        return {pb->dma_buf + TCP_HDR_SZ, tcp_options_len};
    }
}
