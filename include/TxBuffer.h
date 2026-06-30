#pragma once

#include <cstddef>
#include <span>

class TxBuffer {
    friend class SfUdpSocket;
public:
    std::span<std::byte> buf;

    TxBuffer(std::span<std::byte> buf, int id);
private:
    int id;
};
