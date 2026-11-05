///
#include "Slic3r/App/Yoga/Dialog.hpp"

#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/RootItem.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Utils.hpp"

#include <Slic3r/Log.hpp>

namespace Slic3r::App::Yoga {
constexpr Unit dialog_padding = 10_fpx;

namespace {

std::string popup_name(const std::string& dialog_name)
{
    constexpr std::string_view suffix{"Dialog"};
    if (dialog_name.ends_with(suffix)) {
        return dialog_name.substr(0, dialog_name.size() - suffix.size()) + "Popup";
    }
    return dialog_name + "Popup";
}

} // namespace

Dialog::Dialog(const std::string& name)
{
    const std::string dialog_name = name.empty() ? "Dialog" : name;
    set_object_name(popup_name(dialog_name));
    WindowPtr window = std::make_unique<Window>(dialog_name);

    window->set_orientation(Orientation::Vertical);
    window->set_gap(0);
    window->set_padding(0);
    window->set_flags(ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    window->set_max_width(100_ww);
    window->set_max_height(100_wh);

    m_top_row = window->emplace_back<Item>();
    m_top_row->set_max_height(40_fpx);
    m_top_row->set_flex_shrink(0);

    m_tab_container = m_top_row->emplace_back<Item>();

    m_tab_button_group.callbacks().checked_changed =
        [this](AbstractButton* current_checked, AbstractButton* last_checked)
    {
        if (current_checked) {
            std::vector<LayoutButton*>::const_iterator it =
                std::find(m_tab_buttons.cbegin(), m_tab_buttons.cend(), current_checked);

            // This may be a bigger underlying problem.
            if (it == m_tab_buttons.cend()) {
                return;
            }

            const size_t current_index = std::distance(m_tab_buttons.cbegin(), it);
            set_current_tab(current_index);
        }
    };

    Rectangle* buttons_rect = m_top_row->emplace_back<Rectangle>();
    m_buttons_rect          = buttons_rect;
    buttons_rect->set_justify_content(YGJustifyFlexEnd);
    buttons_rect->set_align_items(YGAlignCenter);
    buttons_rect->set_padding(dialog_padding);
    buttons_rect->set_fill(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
    buttons_rect->set_flex_grow(1);
    buttons_rect->set_rounding(0);

    m_drag_handle = buttons_rect->emplace_back<AbstractButton>();
    m_drag_handle->set_flex_grow(1);
    m_drag_handle->set_self_align(YGAlignStretch);
    m_drag_handle->set_visible(false);

    m_pin_button = buttons_rect->emplace_back<LayoutButton>(std::string{}, Render::Icon::Pin);
    m_pin_button->set_min_width(20_fpx);
    m_pin_button->set_min_height(20_fpx);
    m_pin_button->set_checkable(true);
    m_pin_button->set_visible(false);
    m_pin_button->set_tooltip(Biz::_u8L("Pin"));
    m_pin_button->callbacks().action = [this] { set_pinned_internal(m_pin_button->checked()); };

    m_maximize_button =
        buttons_rect->emplace_back<LayoutButton>(std::string{}, Render::Icon::ArrowsDiagonal);
    m_maximize_button->set_min_width(20_fpx);
    m_maximize_button->set_min_height(20_fpx);
    m_maximize_button->set_checkable(true);
    m_maximize_button->set_visible(false);
    m_maximize_button->set_tooltip(Biz::_u8L("Maximize"));
    m_maximize_button->callbacks().action = [this] { set_maximized(m_maximize_button->checked()); };

    m_close_button =
        buttons_rect->emplace_back<LayoutButton>(std::string{}, Render::Icon::PrintIdle);
    m_close_button->set_object_name("DialogCloseButton");
    m_close_button->set_min_width(20_fpx);
    m_close_button->set_min_height(20_fpx);
    m_close_button->callbacks().action = [this] { close_action(); };

    window->callbacks().geometry_changed = [this](const Vec2f& pos, const Vec2f& size)
    {
        Window* content_window = content_item();
        content_window->set_left(pos.x());
        content_window->set_top(pos.y());
        content_window->set_width(size.x());
        content_window->set_height(size.y());
        user_geometry_changed();
    };

    m_content = window->emplace_back<Item>();
    m_content->set_padding(dialog_padding);

    set_content_item(std::move(window));
}

Dialog::Dialog(std::initializer_list<std::string> tabs, const std::string& name) : Dialog(name)
{
    for (const std::string& tab : tabs) {
        append_tab(tab);
    }
}

Dialog::DialogCallbacks& Dialog::dialog_callbacks()
{
    return m_callbacks;
}

bool Dialog::closable() const
{
    return m_closable;
}

void Dialog::set_closable(bool closable)
{
    if (m_closable != closable) {
        m_closable = closable;
        m_close_button->set_visible(m_closable);
    }
}

size_t Dialog::current_tab_index() const
{
    return m_current_tab_index;
}

Item* Dialog::content() const
{
    return m_content;
}

LayoutButton* Dialog::close_button() const
{
    return m_close_button;
}

LayoutButton* Dialog::maximize_button() const
{
    return m_maximize_button;
}

LayoutButton* Dialog::pin_button() const
{
    return m_pin_button;
}

Separator* Dialog::add_separator()
{
    Separator* separator = m_content->emplace_back<Separator>(Orientation::Horizontal);
    separator->set_margin(Margins(-dialog_padding, 0.f));
    return separator;
}

LayoutButton* Dialog::append_tab(const std::string& tab)
{
    LayoutButton* tab_button = m_tab_container->emplace_back<LayoutButton>(tab);
    tab_button->set_object_name("DialogTabButton");
    tab_button->set_checkable(true);
    tab_button->set_flex_grow(m_tab_flex_grow);
    tab_button->set_content_padding({10_fpx, 3_fpx});
    tab_button->set_background_color(Platform::Color::WindowBgAlternate);
    tab_button->set_background_color_checked(m_theme->color_imgui(Platform::Color::WindowBg));

    if (m_tab_buttons.empty()) {
        tab_button->set_checked(true);
        tab_button->set_draw_flags(ImDrawFlags_RoundCornersTopLeft);
    } else {
        tab_button->set_rounding(0);
    }

    m_tab_buttons.push_back(tab_button);
    m_tab_button_group.insert_button(tab_button);

    return tab_button;
}

void Dialog::remove_tab(size_t index)
{
    ASSERT(index < m_tab_buttons.size());
    LayoutButton* button = m_tab_buttons.at(index);
    m_tab_button_group.remove_button(button);
    m_tab_container->remove(m_tab_container->get_item(index));
    m_tab_buttons.erase(m_tab_buttons.cbegin() + index);
}

void Dialog::set_current_tab(size_t current_index)
{
    m_current_tab_index = current_index;
    m_tab_buttons.at(current_index)->set_checked(true); // can be NOOP

    on_tab_selected(current_index);
    if (m_callbacks.tab_selected) {
        m_callbacks.tab_selected(current_index);
    }
}

void Dialog::set_tab_enabled(size_t index, bool enabled)
{
    m_tab_buttons.at(index)->set_enabled(enabled);
}

float Dialog::tab_flex_grow() const
{
    return m_tab_flex_grow;
}

void Dialog::set_tab_flex_grow(float flex_grow)
{
    if (Domain::fuzzy_compare(m_tab_flex_grow, flex_grow)) {
        return;
    }
    m_tab_flex_grow = flex_grow;
    m_tab_container->set_flex_grow(flex_grow);
    m_buttons_rect->set_flex_grow(flex_grow > 0.f ? 0.f : 1.f);
    for (LayoutButton* tab_button : m_tab_buttons) {
        tab_button->set_flex_grow(flex_grow);
    }
}

void Dialog::set_implicit_size(const std::optional<Unit>& width, const std::optional<Unit>& height)
{
    m_implicit_size = ImplicitSize{width, height};
    apply_implicit_size();
}

void Dialog::apply_implicit_size()
{
    Window* window = content_item();
    if (m_implicit_size->width) {
        window->set_width(*m_implicit_size->width);
    } else {
        window->reset_width();
    }
    if (m_implicit_size->height) {
        window->set_height(*m_implicit_size->height);
    } else {
        window->reset_height();
    }
}

void Dialog::on_tab_selected(int current_index) {}

void Dialog::close_action()
{
    close();
}

bool Dialog::resizable() const
{
    return m_resizable;
}

void Dialog::set_resizable(bool resizable)
{
    if (m_resizable != resizable) {
        m_resizable = resizable;
        update_geometry_controls();
    }
}

bool Dialog::movable() const
{
    return m_movable || m_resizable;
}

void Dialog::set_movable(bool movable)
{
    if (m_movable != movable) {
        m_movable = movable;
        update_geometry_controls();
    }
}

bool Dialog::pinnable() const
{
    return m_pinnable;
}

void Dialog::set_pinnable(bool pinnable)
{
    if (m_pinnable == pinnable) {
        return;
    }
    m_pinnable = pinnable;
    if (m_pinnable && content_item()->base_object_name() == "Dialog") {
        SPDLOG_WARN("Pinnable dialog uses the default object name, give it a unique name");
    }
    update_geometry_controls();
}

bool Dialog::maximized() const
{
    return m_maximized;
}

void Dialog::set_maximized(bool maximized)
{
    if (m_maximized != maximized) {
        set_maximized_internal(maximized, current_geometry());
    }
}

void Dialog::set_maximized_internal(bool maximized, const DialogGeometry& restore_geometry)
{
    if (maximized) {
        m_restore_geometry           = restore_geometry;
        m_restore_geometry.maximized = false;
        Window* window               = content_item();
        window->set_left(0);
        window->set_top(0);
        window->set_width(100_ww);
        window->set_height(100_wh);
    } else {
        set_window_geometry(m_restore_geometry);
    }
    m_maximized = maximized;
    m_maximize_button->set_checked(maximized);
    m_maximize_button->set_tooltip(maximized ? Biz::_u8L("Restore") : Biz::_u8L("Maximize"));
    user_geometry_changed();
    update_geometry_controls();
}

bool Dialog::pinned() const
{
    return m_pinned;
}

void Dialog::set_pinned_internal(bool pinned)
{
    if (m_pinned == pinned) {
        return;
    }
    update_pin_state(pinned);
    IDialogGeometryStore* store = geometry_store();
    if (pinned) {
        m_geometry_dirty = true;
    } else if (store) {
        store->remove_dialog_geometry(content_item()->base_object_name());
    }
}

void Dialog::update_pin_state(bool pinned)
{
    m_pinned = pinned;
    m_pin_button->set_checked(pinned);
    m_pin_button->set_tooltip(pinned ? Biz::_u8L("Unpin") : Biz::_u8L("Pin"));
}

IDialogGeometryStore* Dialog::geometry_store() const
{
    const RootItem* root = dynamic_cast<const RootItem*>(root_item());
    return root ? root->dialog_geometry_store() : nullptr;
}

void Dialog::load_geometry()
{
    const Window* window = content_item();
    if (m_resizable && (window->min_width() == Unit{0} || window->min_heigth() == Unit{0})) {
        SPDLOG_WARN("Resizable dialog {} has no min size set", window->base_object_name());
    }
    if (m_resizable && !m_implicit_size) {
        SPDLOG_WARN("Resizable dialog {} has no implicit size set", window->base_object_name());
    }

    std::optional<DialogGeometry> stored;
    IDialogGeometryStore* store = geometry_store();
    if (store && m_pinnable && movable()) {
        stored = store->dialog_geometry(window->base_object_name());
    }
    if (stored) {
        apply_geometry(stored.value());
    }
    update_pin_state(stored.has_value());

    update_geometry_controls();
}

void Dialog::reset_geometry()
{
    if (m_resizable && m_implicit_size) {
        apply_implicit_size();
    } else if (m_maximized) {
        set_window_geometry(m_restore_geometry);
    }
    m_maximized = false;
    m_maximize_button->set_checked(false);
    m_maximize_button->set_tooltip(Biz::_u8L("Maximize"));
    m_geometry_dirty = false;
    update_geometry_controls();
}

void Dialog::render(const Vec2f& pos, const Vec2f& size)
{
    IDialogGeometryStore* store = geometry_store();
    if (m_geometry_dirty && store && pinned()) {
        store->set_dialog_geometry(content_item()->base_object_name(), current_geometry());
        m_geometry_dirty = false;
    }

    if (m_resizable
        && m_drag_handle->hovered()
        && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        set_maximized(!m_maximized);
    } else if (m_drag_handle->pressed() && !m_maximized) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        if (delta.x != 0.f || delta.y != 0.f) {
            Window* window = content_item();
            window->set_left(window->left() + delta.x);
            window->set_top(window->top() + delta.y);
            user_geometry_changed();
        }
    }

    Popup::render(pos, size);
}

void Dialog::update_geometry_controls()
{
    const bool movable   = this->movable();
    const bool resizable = m_resizable;

    m_drag_handle->set_visible(movable);
    m_maximize_button->set_visible(resizable);
    m_pin_button->set_visible(movable && m_pinnable);

    Window* window  = content_item();
    const int flags = resizable && !m_maximized ? window->flags() & ~ImGuiWindowFlags_NoResize :
                                                  window->flags() | ImGuiWindowFlags_NoResize;
    window->set_flags(flags);
}

void Dialog::user_geometry_changed()
{
    detach();
    m_geometry_dirty = true;
}

DialogGeometry Dialog::current_geometry() const
{
    if (m_maximized) {
        DialogGeometry geometry = m_restore_geometry;
        geometry.maximized      = true;
        return geometry;
    }
    const Window* window = content_item();
    return {window->left(), window->top(), window->width(), window->height(), false};
}

void Dialog::apply_geometry(const DialogGeometry& geometry)
{
    detach();
    if (!m_resizable) {
        Window* window = content_item();
        window->set_left(geometry.left);
        window->set_top(geometry.top);
    } else if (geometry.maximized) {
        set_maximized_internal(true, geometry);
    } else {
        set_window_geometry(geometry);
    }
}

void Dialog::set_window_geometry(const DialogGeometry& geometry)
{
    Window* window = content_item();
    window->set_left(geometry.left);
    window->set_top(geometry.top);
    window->set_width(geometry.width);
    window->set_height(geometry.height);
}

} // namespace Slic3r::App::Yoga
