#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/ItemEvents.hpp"

#include <list>

namespace Slic3r::App::Yoga {

class Popup;
class IDialogGeometryStore;

class RootItem : public Item
{
public:
    RootItem();
    ~RootItem();

    void root_render(const SizeInfo& size_info);

    int last_rendered_frame() const;

    void set_style_dirty() override;

    void style_node() override;

    void open_popup(Popup* popup);
    void close_popup(Popup* popup);

    IDialogGeometryStore* dialog_geometry_store() const;
    /**
     * @brief Store used by pinnable dialogs in this tree, nullptr disables persistence
     */
    void set_dialog_geometry_store(IDialogGeometryStore* store);

    void push_event(EventPtr event) override;

    template <class F>
    void for_each_popup_reconcile(F&& fn);

protected:
    /**
     * @note intentionally hidden, use RootItem::root_render
     */
    void render(const Vec2f& pos, const Vec2f& size) override;

    /**
     * @brief Yoga recalculate whole tree
     * @note should be called only top-level item
     */
    void calculate_size();

    void render_debug_overlay();

protected:
    LoopEvents m_loop_events;

    bool m_style_dirty = true;

    SizeInfo m_size_info;

    using Popups = std::list<Popup*>;
    Popups m_popups;
    Popups m_popups_to_be_added;
    Popups m_popups_to_be_deleted;

    IDialogGeometryStore* m_dialog_geometry_store = nullptr;

private:
    int m_last_rendered_frame = -1;
};

} // namespace Slic3r::App::Yoga
