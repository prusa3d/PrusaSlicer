#pragma once

#include "Slic3r/Biz/Arrange/ArrangeItem.hpp"

namespace Slic3r::Biz::Arrange {
struct PackingContext
{
    void add_fixed_item(const ArrangeItem& item);
    void add_fixed_items(const std::vector<ArrangeItem>& items);
    void add_packed_item(const ArrangeItem& item);

    const std::vector<ArrangeItem>& fixed_items() const;
    const std::vector<ArrangeItem>& packed_items() const;
    const std::vector<ArrangeItem>& all_items() const;

private:
    std::vector<ArrangeItem> m_fixed;
    std::vector<ArrangeItem> m_packed;
    std::vector<ArrangeItem> m_all_items;
};
} // namespace Slic3r::Biz::Arrange
