#pragma once

#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"

namespace Slic3r::App {

struct PageEntry
{
    std::string name;
    Render::Icon icon = Render::Icon::None;
    std::optional<bool> is_highlighted_text{ std::nullopt};
};

class PageEntryButton : public Yoga::LayoutButton, public Biz::DataObserver<PageEntry>
{
public:
    using FnIndexClicked = std::function<void(size_t)>;

    PageEntryButton(size_t index, const PageEntry& page_entry, FnIndexClicked on_clicked);

protected:
    void on_data_update() override;

private:
    FnIndexClicked m_on_clicked;
};

} // namespace Slic3r::App
