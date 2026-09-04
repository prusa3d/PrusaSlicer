#include "Slic3r/App/Yoga/RootItem.hpp"

#include "Slic3r/App/Yoga/Popup.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"

#include <imgui_internal.h>
#include <fmt/format.h>

namespace Slic3r::App::Yoga {

RootItem::RootItem() : m_loop_events(*this)
{
    set_object_name("RootItem");
}

RootItem::~RootItem()
{
    // Manually mark all popups as closed (RootItem is deleted first)
    for (Popup* popup : std::as_const(m_popups)) {
        popup->close();
    }

    while (!m_popups_to_be_added.empty()) {
        m_popups_to_be_added.front()->close();
    }
}

void RootItem::render(const Vec2f& pos, const Vec2f& size) {}

template <class F>
void RootItem::for_each_popup_reconcile(F&& fn)
{
    for (Popup* deleted_popup : std::as_const(m_popups_to_be_deleted)) {
        Popups::const_iterator deleted_it =
            std::find(m_popups.cbegin(), m_popups.cend(), deleted_popup);
        ASSERT(deleted_it != m_popups.cend());
        m_popups.erase(deleted_it);
    }
    m_popups_to_be_deleted.clear();

    for (Popups::iterator it = m_popups.begin(); it != m_popups.end();) {
        Popup* current = *it;
        fn(*current);

        if (!m_popups_to_be_deleted.empty()) {
            bool erased_current = false;

            for (Popup* deleted : std::as_const(m_popups_to_be_deleted)) {
                Popups::iterator deleted_it = std::find(m_popups.begin(), m_popups.end(), deleted);

                ASSERT(deleted_it != m_popups.cend());
                if (deleted_it == it) {
                    it             = m_popups.erase(it);
                    erased_current = true;
                } else {
                    m_popups.erase(deleted_it);
                }
            }
            m_popups_to_be_deleted.clear();

            if (erased_current) {
                continue;
            }
        }

        ++it;
    }
}

void RootItem::root_render(const SizeInfo& size_info)
{
    if (size_info.viewport_size_y == 0 || size_info.viewport_size_y == 0) {
        return;
    }

    m_size_info = size_info;
    const Vec2f size{size_info.viewport_size_x, size_info.viewport_size_y};

    if (!m_popups_to_be_added.empty()) {
        std::copy(
            m_popups_to_be_added.cbegin(),
            m_popups_to_be_added.cend(),
            std::back_inserter(m_popups)
        );
        m_popups_to_be_added.clear();
    }

    style_node();
    m_style_dirty = false;
    calculate_size();
    if (m_style_dirty) {
        style_node();
        calculate_size();
    }

    render_item_begin({}, size);

    render_item_end({}, size);

    for_each_popup_reconcile([&](Popup& popup) { popup.render({}, size); });

    render_debug_overlay();

    if (m_loop_events.process_events() || m_style_dirty) {
        // There were a number of processed events,
        // therefore we need to schedule another render pass
        // OR
        // Some item called set_style_dirty in render()
        Biz::Platform::PlatformServices::instance().render_request_handler().request_render();
    }
}

void RootItem::set_style_dirty()
{
    m_style_dirty = true;
}

void RootItem::push_event(EventPtr event)
{
    m_loop_events.insert_event(std::move(event));
}

void RootItem::render_debug_overlay()
{
#ifdef DEBUG
    if (m_debug_item) {
        m_debug_item->render_debug_overlay(ImGui::GetForegroundDrawList());
        m_debug_item = nullptr;
    }
#endif
}

void RootItem::calculate_size()
{
    // Force whole tree to update its EvaluatedUnits
    resize(m_size_info);
    for_each_popup_reconcile(
        [&](Popup& popup)
        {
            if (popup.content_item())
                popup.content_item()->resize(m_size_info);
        }
    );

    // Calculate layout for root node
    YGNodeCalculateLayout(
        node(),
        static_cast<float>(m_size_info.viewport_size_x),
        static_cast<float>(m_size_info.viewport_size_y),
        YGDirectionLTR
    );
    // Calculate layout for popup nodes
    for_each_popup_reconcile([&](Popup& popup) { popup.resize(m_size_info); });

    check_resized();
    for_each_popup_reconcile([&](Popup& popup) { popup.check_resized(); });
}

void RootItem::style_node()
{
    Item::style_node();

    for_each_popup_reconcile([&](Popup& popup) { popup.style_node(); });
}

void RootItem::open_popup(Popup* popup)
{
    ASSERT(popup);
    ASSERT(popup->content_item());

    Popups::const_iterator it =
        std::find(m_popups_to_be_deleted.cbegin(), m_popups_to_be_deleted.cend(), popup);
    if (it != m_popups_to_be_deleted.cend()) {
        m_popups_to_be_deleted.erase(it);
    }

    if (std::find(m_popups.cbegin(), m_popups.cend(), popup) != m_popups.cend()
        || std::find(m_popups_to_be_added.cbegin(), m_popups_to_be_added.cend(), popup)
            != m_popups_to_be_added.cend())
    {
        return;
    }

    m_popups_to_be_added.push_back(popup);
}

void RootItem::close_popup(Popup* popup)
{
    if (std::find(m_popups_to_be_deleted.cbegin(), m_popups_to_be_deleted.cend(), popup)
        != m_popups_to_be_deleted.cend())
    {
        // Popup is already scheduled to be deleted
        return;
    }

    Popups::const_iterator it =
        std::find(m_popups_to_be_added.cbegin(), m_popups_to_be_added.cend(), popup);
    if (it != m_popups_to_be_added.cend()) {
        // Popup was just scheduled to be opened
        m_popups_to_be_added.erase(it);
        return;
    }

    if (std::find(m_popups.cbegin(), m_popups.cend(), popup) != m_popups.cend()) {
        // Popup is scheduled to be deleted
        m_popups_to_be_deleted.push_back(popup);
    }
}

} // namespace Slic3r::App::Yoga
