#pragma once

#include "types.h"

constexpr int PKT_BUF_SZ = 64;

constexpr int ETH_HDR_SZ = 14;
constexpr int IP_HDR_SZ = 20;
constexpr int UDP_HDR_SZ = 8;
constexpr int TCP_HDR_SZ = 20;
constexpr int UDP_TOTAL_HDR_SZ = ETH_HDR_SZ + IP_HDR_SZ + UDP_HDR_SZ;
constexpr int TCP_TOTAL_HDR_SZ = ETH_HDR_SZ + IP_HDR_SZ + TCP_HDR_SZ;

constexpr int UDP_TOTAL_METADATA_SZ = UDP_TOTAL_HDR_SZ + PKT_BUF_SZ;
constexpr int TCP_TOTAL_METADATA_SZ = TCP_TOTAL_HDR_SZ + PKT_BUF_SZ;
constexpr int TCP_MAX_PAYLOAD_SZ = io::BUF_SZ - TCP_TOTAL_METADATA_SZ - 64;

constexpr u32 SYN_FLAG = 0b00000010;
constexpr u32 ACK_FLAG = 0b00010000;
constexpr u32 FIN_FLAG = 0b00000001;
constexpr u32 RST_FLAG = 0b00000100;

constexpr int IP_S_ADDR_OFFSET = 16;
constexpr int IP_D_ADDR_OFFSET = 20;
constexpr int TCP_SEQ_NUM_OFFSET = 32;
constexpr int TCP_ACK_NUM_OFFSET = 36;

constexpr int UDP_S_PORT_OFFSET = 24;
constexpr int UDP_D_PORT_OFFSET = 26;
constexpr int UDP_LEN_OFFSET = 28;

constexpr int RTO_MILLISECONDS = 200;
constexpr int CONNECT_TIMEOUT_MILLISECONDS = 1000;

namespace net {
    // FIELDS ARE IN NETWORK ORDER

    // 14 bytes
    struct eth_hdr {
        u8 dmac[6];
        u8 smac[6];
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

    // 8 bytes
    struct udp_hdr {
        u16 src_port;
        u16 dst_port;
        u16 len;
        u16 checksum;
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
        auto* ip = get_ip_hdr(pb);
        auto* tcp = get_tcp_hdr(pb);

        int tcp_header_len = (tcp->doffset_reserved >> 4) * 4;
        u32 payload_sz = from_net(ip->len) - IP_HDR_SZ - tcp_header_len;

        return {reinterpret_cast<std::byte*>(tcp) + tcp_header_len, payload_sz};
    }

    inline std::span<std::byte> get_tcp_options(io::pkt_buf* pb) {
        auto* tcp = get_tcp_hdr(pb);
        u32 tcp_header_len = (tcp->doffset_reserved >> 4) * 4;
        u32 tcp_options_len = tcp_header_len - TCP_HDR_SZ;
        return {pb->dma_buf + TCP_HDR_SZ, tcp_options_len};
    }
}
