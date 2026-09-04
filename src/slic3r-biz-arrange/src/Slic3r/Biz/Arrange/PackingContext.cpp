#include "Slic3r/Biz/Arrange/PackingContext.hpp"

namespace Slic3r::Biz::Arrange {
void PackingContext::add_fixed_items(const std::vector<ArrangeItem>& items)
{
    m_fixed.insert(m_fixed.end(), items.begin(), items.end());
    m_all_items.insert(m_all_items.end(), items.begin(), items.end());
}

void PackingContext::add_packed_item(const ArrangeItem& item)
{
    m_packed.push_back(item);
    m_all_items.push_back(item);
}

const std::vector<ArrangeItem>& PackingContext::fixed_items() const
{
    return m_fixed;
}

const std::vector<ArrangeItem>& PackingContext::packed_items() const
{
    return m_packed;
}

const std::vector<ArrangeItem>& PackingContext::all_items() const
{
    return m_all_items;
}
} // namespace Slic3r::Biz::Arrange
