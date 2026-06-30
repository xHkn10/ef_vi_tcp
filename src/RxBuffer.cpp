#include "RxBuffer.h"
#include "ef_vi_stuff.h"

#include <span>

RxBuffer::RxBuffer(std::span<std::byte> buf, ef_struct *ef, int id)
    : buf{buf}, ef{ef}, id{id} {}

RxBuffer::~RxBuffer() {
    if (ef == nullptr)
        return;
    ef->rx_free_stk.push_back(id);
}

RxBuffer::RxBuffer(RxBuffer &&other) noexcept {
    buf = other.buf;
    ef = other.ef;
    id = other.id;
    other.ef = nullptr;
}

RxBuffer& RxBuffer::operator=(RxBuffer &&other) noexcept {
    if (this != &other) {
        if (ef != nullptr)
            ef->rx_free_stk.push_back(id);
        buf = other.buf;
        ef = other.ef;
        id = other.id;
        other.ef = nullptr;
    }
    return *this;
}
