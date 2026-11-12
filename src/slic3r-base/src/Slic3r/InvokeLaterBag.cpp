#include "Slic3r/InvokeLaterBag.hpp"

namespace Slic3r {

InvokeLaterBag::~InvokeLaterBag()
{
    invoke_now();
}

void InvokeLaterBag::invoke_now()
{
    for (const auto& rec : m_to_invoke) {
        rec.func();
    }
    m_to_invoke.clear();
}

void InvokeLaterBag::add(Func&& func, const Tag& dedup_tag)
{
    if (dedup_tag.has_value()) {
        std::erase_if(
            m_to_invoke,
            [&dedup_tag](const auto& rec) { return rec.dedup_tag == dedup_tag; }
        );
    }
    m_to_invoke.emplace_back(std::forward<Func>(func), dedup_tag);
}

} // namespace Slic3r::Biz
