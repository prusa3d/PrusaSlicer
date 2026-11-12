#pragma once

#include "Slic3r/App/Yoga/Dialog.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/PageEntryButton.hpp"

namespace Slic3r::Domain {
class ConfigItem;
} // namespace Slic3r::Domain

namespace Slic3r::App::Yoga {
class StackLayout;
class LayoutButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Yoga {

class AbstractSettingsDialog : public Dialog
{
public:
    explicit AbstractSettingsDialog(
        const std::initializer_list<std::string>& tabs,
        const std::string& name = {}
    );

    static LayoutButton* add_footer_button(
        Item* footer,
        const std::string& label,
        Render::Icon icon = Render::Icon::None
    );

protected:
    using PageListView = Yoga::ListView<
        PageEntryButton,
        PageEntry,
        Yoga::ViewFactory<PageEntryButton, PageEntry, PageEntryButton::FnIndexClicked>>;

    struct RowItem
    {
        ItemPtr input;
        std::string label;
        std::string symbol;
    };

    struct Tab
    {
        PageListView* page_list_view          = nullptr;
        Yoga::StackLayout* pages_stack_layout = nullptr;
        Yoga::Item* tab_item                  = nullptr;

        void replace_stack_layout(std::unique_ptr<Yoga::StackLayout> stack_layout);
    };

    void on_tab_selected(int current_index) override;

    Tab* append_tab(const std::string& tab);
    virtual void remove_tab(size_t index);

    LayoutButton*
    add_footer_button(const std::string& label, Render::Icon icon = Render::Icon::None);

protected:
    using TabPtr = std::unique_ptr<Tab>;
    using Tabs   = std::vector<TabPtr>;
    Tabs m_tabs;
    Item* m_footer                  = nullptr;
    Tab* m_current_tab              = nullptr;
    Yoga::StackLayout* m_stack_tabs = nullptr;
    bool m_remove_in_progress       = false;
};

} // namespace Slic3r::App::Yoga
