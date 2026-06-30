#pragma once

#include "ef_vi_stuff.h"

class RxOooList {
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

    void insert(pkt_buf* new_pb) {
        if (!head || new_pb < head) {
            new_pb->meta.nxt = head;
            head = new_pb;
            return;
        }
        pkt_buf* cur = head;
        while (cur->meta.nxt && cur < new_pb)
            cur = cur->meta.nxt;
        if (cur->meta.seq == new_pb->meta.seq)
            return;
        new_pb->meta.nxt = cur->meta.nxt;
        cur->meta.nxt = new_pb;
    }

private:
    pkt_buf* head = nullptr;
};
