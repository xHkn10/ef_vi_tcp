#pragma once

#include "ef_vi_stuff.h"

class rx_ooo_list {
public:
    [[nodiscard]] bool empty() const {
        return head == nullptr;
    }

    [[nodiscard]] pkt_buf* peek_front() const {
        return head;
    }

    pkt_buf* pop_front() {
        if (!head)
            return nullptr;
        auto ret = head;
        head = head->meta.nxt;
        return ret;
    }

    bool insert(pkt_buf* new_pb) {
        if (!head || *new_pb < *head) {
            new_pb->meta.nxt = head;
            head = new_pb;
            return true;
        }
        pkt_buf* cur = head;
        while (cur->meta.nxt && *cur->meta.nxt < *new_pb)
            cur = cur->meta.nxt;
        if (cur->meta.nxt->meta.seq == new_pb->meta.seq)
            return false;
        new_pb->meta.nxt = cur->meta.nxt;
        cur->meta.nxt = new_pb;
        return true;
    }

private:
    pkt_buf* head = nullptr;
};
