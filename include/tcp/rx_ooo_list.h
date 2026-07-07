#pragma once

#include "io/context.h"

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
            auto ret = head;
            head = head->meta.nxt;
            return ret;
        }

        bool insert(io::pkt_buf* new_pb) {
            if (!head || *new_pb < *head) {
                new_pb->meta.nxt = head;
                head = new_pb;
                return true;
            }
            io::pkt_buf* cur = head;
            while (cur->meta.nxt && *cur->meta.nxt < *new_pb)
                cur = cur->meta.nxt;
            if (cur->meta.nxt && cur->meta.nxt->meta.seq == new_pb->meta.seq)
                return false;
            new_pb->meta.nxt = cur->meta.nxt;
            cur->meta.nxt = new_pb;
            return true;
        }

    private:
        io::pkt_buf* head = nullptr;
    };
}
