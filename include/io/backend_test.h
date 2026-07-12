#pragma once
#include <cstddef>
#include <span>

#include "context.h"

namespace io::test {
    void test_inject(context& ctx, std::span<const std::byte> frame);
    void test_reset();
    std::vector<std::vector<std::byte>>& test_captured();
}
