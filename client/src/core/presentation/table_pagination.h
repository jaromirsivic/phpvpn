#pragma once
#include <algorithm>
#include <vector>
namespace phpvpn {
inline constexpr int kTablePageSize = 20;
inline int tablePageCount(std::size_t rows) {
    return (std::max)(1, (static_cast<int>(rows) + kTablePageSize - 1) / kTablePageSize);
}
inline std::vector<int> tablePageNumbers(int count, int current) {
    if (count <= 1)
        return {0};
    std::vector<int> pages{0};
    const int start = count <= 7 ? 1 : std::clamp(current - 2, 1, count - 6);
    const int end = count <= 7 ? count - 2 : start + 4;
    for (int page = start; page <= end; ++page)
        pages.push_back(page);
    pages.push_back(count - 1);
    return pages;
}
} // namespace phpvpn
