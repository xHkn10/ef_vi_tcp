#pragma once

#include <cstddef>
#include <span>

#include "ef_vi_stuff.h"

class RxBuffer {
public:
    std::span<std::byte> buf;

    RxBuffer(std::span<std::byte> buf, ef_struct* ef, int id);
    ~RxBuffer();

    RxBuffer(RxBuffer&& other) noexcept;
    RxBuffer& operator=(RxBuffer&& other) noexcept;

    RxBuffer(const RxBuffer&) = delete;
    RxBuffer& operator=(const RxBuffer&) = delete;

private:
    ef_struct* ef;
    int id;
};
