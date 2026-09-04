#include "Slic3r/App/Yoga/AbstractSettingsDialog.hpp"

#include "Slic3r/App/Yoga/StackLayout.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"

using namespace Slic3r::App::Yoga;
using namespace Slic3r::App::Render;

namespace Slic3r::App::Yoga {

AbstractSettingsDialog::AbstractSettingsDialog(
    const std::initializer_list<std::string>& tabs,
    const std::string& name
) :
    Dialog(name.empty() ? "AbstractSettingsDialog" : name)
{
    content_item()->set_width(700);
    content_item()->set_height(700);

    content()->set_orientation(Orientation::Vertical);
    content()->set_gap(0);
    content()->set_flex_grow(1);
    // Only preserve 1px bottom padding
    content()->set_padding(Paddings(0, 0, 0, 1));

    m_stack_tabs = content()->emplace_back<StackLayout>();
    m_stack_tabs->set_orientation(Orientation::Vertical);
    m_stack_tabs->set_flex_grow(1);

    add_separator();

    m_footer = content()->emplace_back<Item>();
    m_footer->set_padding(10);
    m_footer->set_justify_content(YGJustifyFlexEnd);
    m_footer->set_self_align(YGAlignFlexEnd);
    m_footer->set_flex_shrink(0);

    for (const std::string& tab : tabs)
        append_tab(tab);
}

LayoutButton*
AbstractSettingsDialog::add_footer_button(Item* footer, const std::string& label, Render::Icon icon)
{
    LayoutButton* button = footer->emplace_back<LayoutButton>(label, icon);
    button->set_content_padding({7.f});
    button->set_height({30.f});
    return button;
}

void AbstractSettingsDialog::on_tab_selected(int current_index)
{
    if (m_remove_in_progress) {
        return;
    }

    if (!m_tabs.empty()) {
        m_current_tab = m_tabs.at(current_index).get();
        m_stack_tabs->set_current_index(current_index);
    } else {
        m_current_tab = nullptr;
    }
}

void AbstractSettingsDialog::remove_tab(size_t index)
{
    Tab* tab_to_delete = m_tabs.at(index).get();

    m_remove_in_progress = true;
    m_stack_tabs->remove(m_stack_tabs->get_item(index));
    m_tabs.erase(m_tabs.cbegin() + index);
    Dialog::remove_tab(index);
    m_remove_in_progress = false;

    if (m_current_tab == tab_to_delete && !m_tabs.empty()) {
        set_current_tab(0);
    }
}

LayoutButton* AbstractSettingsDialog::add_footer_button(const std::string& label, Render::Icon icon)
{
    return add_footer_button(m_footer, label, icon);
}

AbstractSettingsDialog::Tab* AbstractSettingsDialog::append_tab(const std::string& tab)
{
    Item* tab_item = m_stack_tabs->emplace_back<Item>();
    tab_item->set_orientation(Orientation::Horizontal);
    tab_item->set_flex_grow(1);

    // Create the ViewFactory explicitly:
    auto factory = Yoga::ViewFactory<PageEntryButton, PageEntry, PageEntryButton::FnIndexClicked>(
        [this](size_t index)
        {
            m_current_tab->pages_stack_layout->set_current_index(index);
            for (size_t button_index = 0;
                 button_index < m_current_tab->page_list_view->object_count();
                 ++button_index)
            {
                PageEntryButton* button = dynamic_cast<PageEntryButton*>(
                    m_current_tab->page_list_view->get_item(button_index)
                );
                ASSERT(button);
                button->set_checked(index == button_index);
            }
        }
    );
    PageListView* page_list_view = tab_item->emplace_back<PageListView>(std::move(factory));
    page_list_view->set_orientation(Orientation::Vertical);
    page_list_view->set_min_width(125);
    page_list_view->set_flex_shrink(0);

    tab_item->emplace_back<Separator>(Orientation::Vertical);

    StackLayout* pages_stack_layout = tab_item->emplace_back<StackLayout>();
    pages_stack_layout->set_orientation(Orientation::Vertical);
    pages_stack_layout->set_flex_grow(1);

    m_tabs.emplace_back(std::make_unique<Tab>(page_list_view, pages_stack_layout, tab_item));

    Dialog::append_tab(tab);

    if (m_tabs.size() == 1) {
        on_tab_selected(0);
    }

    return m_tabs.back().get();
}

void AbstractSettingsDialog::Tab::replace_stack_layout(std::unique_ptr<StackLayout> stack_layout)
{
    ASSERT(stack_layout);

    tab_item->remove(pages_stack_layout);
    pages_stack_layout = stack_layout.get();
    tab_item->insert(std::move(stack_layout), tab_item->object_count());

    pages_stack_layout->set_orientation(Orientation::Vertical);
    pages_stack_layout->set_flex_grow(1);
}

} // namespace Slic3r::App::Yoga
