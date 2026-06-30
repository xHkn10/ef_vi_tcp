#include "TxBuffer.h"

TxBuffer::TxBuffer(std::span<std::byte> buf, int id) {
    this->buf = buf;
    this->id = id;
}
