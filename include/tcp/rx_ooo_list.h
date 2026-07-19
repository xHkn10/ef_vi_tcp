#pragma once

#include "io/context.h"

// TODO consider skipping rx_ooo_list ENTIRELY, by appending directly to ready_queue if received in order

namespace tcp {
    class rx_ooo_list {
    public:
        [[nodiscard]] bool empty() const {
            return head == nullptr;
        }

        [[nodiscard]] io::pkt_buf* peek_front() const {
            return head;
        }

        io::pkt_buf* pop_front() {
            if (!head)
                return nullptr;
            --sz;
            auto ret = head;
            head = head->meta.nxt;
            return ret;
        }

        bool insert(io::pkt_buf* new_pb) {
            if (!head) {
                head = tail = new_pb;
                ++sz;
                return true;
            }

            if (*tail < *new_pb) [[likely]] {
                if (sz >= io::N_RX_BUFS / 2 - 1) [[unlikely]]
                    return false;
                new_pb->meta.nxt = nullptr;
                tail->meta.nxt = new_pb;
                tail = new_pb;
                ++sz;
                return true;
            }

            if (*new_pb < *head) {
                new_pb->meta.nxt = head;
                head = new_pb;
                ++sz;
                return true;
            }

            if (head->meta.seq == new_pb->meta.seq)
                return false;

            auto* cur = head;
            while (cur->meta.nxt && *cur < *new_pb)
                cur = cur->meta.nxt;

            if (cur->meta.nxt && cur->meta.nxt->meta.seq == new_pb->meta.seq)
                return false;

            new_pb->meta.nxt = cur->meta.nxt;
            cur->meta.nxt = new_pb;

            ++sz;
            return true;
        }

        [[nodiscard]] int size() const {
            return sz;
        }

    private:
        io::pkt_buf* head = nullptr;
        io::pkt_buf* tail = nullptr;
        int sz = 0;
    };
}
