///|/ Copyright (c) Prusa Research 2018 - 2022 Enrico Turri @enricoturri1966, David Kocík @kocikdav, Lukáš Matěna @lukasmatena, Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"

#include "GLToolbar.hpp"

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/Plater.hpp"

#include <wx/event.h>
#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/settings.h>
#include <wx/glcanvas.h>

namespace Slic3r {
namespace GUI {

wxDEFINE_EVENT(EVT_GLTOOLBAR_ADD, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_DELETE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_DELETE_ALL, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_ARRANGE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_ARRANGE_CURRENT_BED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_COPY, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_PASTE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_MORE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_FEWER, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_SPLIT_OBJECTS, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_SPLIT_VOLUMES, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_LAYERSEDITING, SimpleEvent);

wxDEFINE_EVENT(EVT_GLVIEWTOOLBAR_3D, SimpleEvent);
wxDEFINE_EVENT(EVT_GLVIEWTOOLBAR_PREVIEW, SimpleEvent);

const GLToolbarItem::ActionCallback GLToolbarItem::Default_Action_Callback = [](){};
const GLToolbarItem::VisibilityCallback GLToolbarItem::Default_Visibility_Callback = []()->bool { return true; };
const GLToolbarItem::EnablingCallback GLToolbarItem::Default_Enabling_Callback = []()->bool { return true; };
const GLToolbarItem::RenderCallback GLToolbarItem::Default_Render_Callback = [](float, float, float, float){};

GLToolbarItem::Data::Option::Option()
    : toggable(false)
    , action_callback(Default_Action_Callback)
    , render_callback(nullptr)
{
}

GLToolbarItem::Data::Data()
    : name("")
    , icon_filename("")
    , tooltip("")
    , additional_tooltip("")
    , sprite_id(-1)
    , visible(true)
    , visibility_callback(Default_Visibility_Callback)
    , enabling_callback(Default_Enabling_Callback)
{
}

GLToolbarItem::GLToolbarItem(GLToolbarItem::EType type, const GLToolbarItem::Data& data)
    : m_type(type)
    , m_state(Normal)
    , m_data(data)
    , m_last_action_type(Undefined)
    , m_highlight_state(NotHighlighted)
{
}

bool GLToolbarItem::update_visibility()
{
    bool visible = m_data.visibility_callback();
    bool ret = (m_data.visible != visible);
    if (ret)
        m_data.visible = visible;
    // Return false for separator as it would always return true.
    return is_separator() ? false : ret;
}

bool GLToolbarItem::update_enabled_state()
{
    bool enabled = m_data.enabling_callback();
    bool ret = (is_enabled() != enabled);
    if (ret)
        m_state = enabled ? GLToolbarItem::Normal : GLToolbarItem::Disabled;

    return ret;
}

void GLToolbarItem::render(const GLCanvas3D& parent, unsigned int tex_id, float left, float right, float bottom, float top, unsigned int tex_width, unsigned int tex_height, unsigned int icon_size) const
{
    auto uvs = [this](unsigned int tex_width, unsigned int tex_height, unsigned int icon_size) -> GLTexture::Quad_UVs {
        assert(tex_width != 0 && tex_height != 0);
        GLTexture::Quad_UVs ret;
        // tiles in the texture are spaced by 1 pixel
        const float icon_size_px = (float)(tex_width - 1) / ((float)Num_States + (float)Num_Rendered_Highlight_States);
        const char render_state = m_highlight_state == NotHighlighted ?
                                  (m_state==Pressed      ? Normal :             // use regular icon, without black background
                                   m_state==HoverPressed ? Hover  : m_state ) : // use regular icon, without black background
                                  Num_States + m_highlight_state;
        const float inv_tex_width = 1.0f / (float)tex_width;
        const float inv_tex_height = 1.0f / (float)tex_height;
        // tiles in the texture are spaced by 1 pixel
        const float u_offset = 1.0f * inv_tex_width;
        const float v_offset = 1.0f * inv_tex_height;
        const float du = icon_size_px * inv_tex_width;
        const float dv = icon_size_px * inv_tex_height;
        const float left = u_offset + (float)render_state * du;
        const float right = left + du - u_offset;
        const float top = v_offset + (float)m_data.sprite_id * dv;
        const float bottom = top + dv - v_offset;
        ret.left_top = { left, top };
        ret.left_bottom = { left, bottom };
        ret.right_bottom = { right, bottom };
        ret.right_top = { right, top };
        return ret;
    };

    GLTexture::render_sub_texture(tex_id, left, right, bottom, top, uvs(tex_width, tex_height, icon_size));

    if (is_pressed()) {
        const Size cnv_size = parent.get_canvas_size();
        const float cnv_w = (float)cnv_size.get_width();
        const float cnv_h = (float)cnv_size.get_height();

        const float out_left   = (0.5f * left + 0.5f) * cnv_w;
        const float out_right  = (0.5f * right + 0.5f) * cnv_w;
        const float out_top    = (0.5f * top + 0.5f) * cnv_h;
        const float out_bottom = (0.5f * bottom + 0.5f) * cnv_h;
        if (m_last_action_type == Left && m_data.left.can_render())
            m_data.left.render_callback(out_left, out_right, out_bottom, out_top);
        else if (m_last_action_type == Right && m_data.right.can_render())
            m_data.right.render_callback(out_left, out_right, out_bottom, out_top);
    }
}

BackgroundTexture::Metadata::Metadata()
    : filename("")
    , left(0)
    , right(0)
    , top(0)
    , bottom(0)
{
}

const float GLToolbar::Default_Icons_Size = 36.f;// 40.0f;

GLToolbar::Layout::Layout()
    : type(Horizontal)
    , horizontal_orientation(HO_Center)
    , vertical_orientation(VO_Center)
    , top(0.0f)
    , left(0.0f)
    , border(5.0f)
    , separator_size(5.0f)
    , gap_size(6.0f)
    , icons_size(Default_Icons_Size)
    , scale(1.0f)
    , width(0.0f)
    , height(0.0f)
    , dirty(true)
{
}

GLToolbar::GLToolbar(GLToolbar::EType type, const std::string& name)
    : m_type(type)
    , m_name(name)
    , m_enabled(false)
    , m_icons_texture_dirty(true)
    , m_pressed_toggable_id(-1)
{
}

GLToolbar::~GLToolbar()
{
    for (GLToolbarItem* item : m_items)
    {
        delete item;
    }
}

bool GLToolbar::init(const BackgroundTexture::Metadata& background_texture)
{
    if (m_background_texture.texture.get_id() != 0)
        return true;

    std::string path = resources_dir() + "/icons/";
    bool res = false;

    if (!background_texture.filename.empty())
        res = m_background_texture.texture.load_from_file(path + background_texture.filename, false, GLTexture::SingleThreaded, false);

    if (res)
        m_background_texture.metadata = background_texture;

    return res;
}

bool GLToolbar::init_arrow(const std::string& filename)
{
    if (m_arrow_texture.get_id() != 0)
        return true;

    const std::string path = resources_dir() + "/icons/";
    return (!filename.empty()) ? m_arrow_texture.load_from_svg_file(path + filename, false, false, false, 512) : false;
}

GLToolbar::Layout::EType GLToolbar::get_layout_type() const
{
    return m_layout.type;
}

void GLToolbar::set_layout_type(GLToolbar::Layout::EType type)
{
    m_layout.type = type;
    m_layout.dirty = true;
}

void GLToolbar::set_position(float top, float left)
{
    m_layout.top = top;
    m_layout.left = left;
}

void GLToolbar::set_border(float border)
{
    m_layout.border = border;
    m_layout.dirty = true;
}

void GLToolbar::set_separator_size(float size)
{
    m_layout.separator_size = size;
    m_layout.dirty = true;
}

void GLToolbar::set_gap_size(float size)
{
    m_layout.gap_size = size;
    m_layout.dirty = true;
}

void GLToolbar::set_icons_size(float size)
{
    if (m_layout.icons_size != size) {
        m_layout.icons_size = size;
        m_layout.dirty = true;
        m_icons_texture_dirty = true;
    }
}

void GLToolbar::set_scale(float scale)
{
    if (m_layout.scale != scale) {
        m_layout.scale = scale;

        m_layout.icons_size     = int(Default_Icons_Size * scale);
        m_layout.border         = int(5.f * scale);
        m_layout.separator_size = int(5.f * scale);
        m_layout.gap_size       = int(6.f * scale);

        m_layout.dirty = true;
        m_icons_texture_dirty = true;
    }
}

bool GLToolbar::add_item(const GLToolbarItem::Data& data)
{
    GLToolbarItem* item = new GLToolbarItem(GLToolbarItem::Action, data);
    if (item == nullptr)
        return false;

    m_items.push_back(item);
    m_layout.dirty = true;
    return true;
}

bool GLToolbar::add_separator()
{
    GLToolbarItem::Data data;
    GLToolbarItem* item = new GLToolbarItem(GLToolbarItem::Separator, data);
    if (item == nullptr)
        return false;

    m_items.push_back(item);
    m_layout.dirty = true;
    return true;
}

float GLToolbar::get_width()
{
    if (m_layout.dirty)
        calc_layout();

    return m_layout.width;
}

float GLToolbar::get_height()
{
    if (m_layout.dirty)
        calc_layout();

    return m_layout.height;
}

void GLToolbar::select_item(const std::string& name)
{
    if (is_item_disabled(name))
        return;

    for (GLToolbarItem* item : m_items)
    {
        if (!item->is_disabled())
        {
            bool hover = item->is_hovered();
            item->set_state((item->get_name() == name) ? (hover ? GLToolbarItem::HoverPressed : GLToolbarItem::Pressed) : (hover ? GLToolbarItem::Hover : GLToolbarItem::Normal));
        }
    }
}

bool GLToolbar::is_item_pressed(const std::string& name) const
{
    for (const GLToolbarItem* item : m_items)
    {
        if (item->get_name() == name)
            return item->is_pressed();
    }

    return false;
}

bool GLToolbar::is_item_disabled(const std::string& name) const
{
    for (const GLToolbarItem* item : m_items)
    {
        if (item->get_name() == name)
            return item->is_disabled();
    }

    return false;
}

bool GLToolbar::is_item_visible(const std::string& name) const
{
    for (const GLToolbarItem* item : m_items)
    {
        if (item->get_name() == name)
            return item->is_visible();
    }

    return false;
}

bool GLToolbar::is_any_item_pressed() const
{
    for (const GLToolbarItem* item : m_items)
    {
        if (item->is_pressed())
            return true;
    }

    return false;
}

int GLToolbar::get_item_id(const std::string& name) const
{
    for (int i = 0; i < (int)m_items.size(); ++i)
    {
        if (m_items[i]->get_name() == name)
            return i;
    }

    return -1;
}

std::string GLToolbar::get_tooltip() const
{
    std::string tooltip;

    for (GLToolbarItem* item : m_items)
    {
        if (item->is_hovered())
        {
            tooltip = item->get_tooltip();
            if (!item->is_pressed())
            {
                const std::string& additional_tooltip = item->get_additional_tooltip();
                if (!additional_tooltip.empty())
                    tooltip += "\n" + additional_tooltip;

                break;
            }
        }
    }

    return tooltip;
}

void GLToolbar::get_additional_tooltip(int item_id, std::string& text)
{
    if (0 <= item_id && item_id < (int)m_items.size())
    {
        text = m_items[item_id]->get_additional_tooltip();
        return;
    }

    text.clear();
}

void GLToolbar::set_additional_tooltip(int item_id, const std::string& text)
{
    if (0 <= item_id && item_id < (int)m_items.size())
        m_items[item_id]->set_additional_tooltip(text);
}

void GLToolbar::set_tooltip(int item_id, const std::string& text)
{
    if (0 <= item_id && item_id < (int)m_items.size())
        m_items[item_id]->set_tooltip(text);
}

bool GLToolbar::update_items_state()
{
    bool ret = false;
    ret |= update_items_visibility();
    ret |= update_items_enabled_state();
    if (!is_any_item_pressed())
        m_pressed_toggable_id = -1;

    return ret;
}

void GLToolbar::render(const GLCanvas3D& parent)
{
    if (!m_enabled || m_items.empty())
        return;

    if (m_icons_texture_dirty)
        generate_icons_texture();

    switch (m_layout.type)
    {
    default:
    case Layout::Horizontal: { render_horizontal(parent); break; }
    case Layout::Vertical:   { render_vertical(parent); break; }
    }
}

bool GLToolbar::on_mouse(wxMouseEvent& evt, GLCanvas3D& parent)
{
    if (!m_enabled)
        return false;

    const Vec2d mouse_pos((double)evt.GetX(), (double)evt.GetY());
    bool processed = false;

    // mouse anywhere
    if (!evt.Dragging() && !evt.Leaving() && !evt.Entering() && m_mouse_capture.parent != nullptr) {
        if (m_mouse_capture.any() && (evt.LeftUp() || evt.MiddleUp() || evt.RightUp())) {
            // prevents loosing selection into the scene if mouse down was done inside the toolbar and mouse up was down outside it,
            // as when switching between views
            m_mouse_capture.reset();
            return true;
        }
        m_mouse_capture.reset();
    }

    if (evt.Moving())
        update_hover_state(mouse_pos, parent);
    else if (evt.LeftUp()) {
        if (m_mouse_capture.left) {
            processed = true;
            m_mouse_capture.left = false;
        }
        else
            return false;
    }
    else if (evt.MiddleUp()) {
        if (m_mouse_capture.middle) {
            processed = true;
            m_mouse_capture.middle = false;
        }
        else
            return false;
    }
    else if (evt.RightUp()) {
        if (m_mouse_capture.right) {
            processed = true;
            m_mouse_capture.right = false;
        }
        else
            return false;
    }
    else if (evt.Dragging()) {
        if (m_mouse_capture.any())
            // if the button down was done on this toolbar, prevent from dragging into the scene
            processed = true;
        else
            return false;
    }

    const int item_id = contains_mouse(mouse_pos, parent);
    if (item_id != -1) {
        // mouse inside toolbar
        if (evt.LeftDown() || evt.LeftDClick()) {
            m_mouse_capture.left = true;
            m_mouse_capture.parent = &parent;
            processed = true;
            if (item_id != -2 && !m_items[item_id]->is_separator() && !m_items[item_id]->is_disabled() &&
                (m_pressed_toggable_id == -1 || m_items[item_id]->get_last_action_type() == GLToolbarItem::Left)) {
                // mouse is inside an icon
                do_action(GLToolbarItem::Left, item_id, parent, true);
                parent.set_as_dirty();
            }
        }
        else if (evt.MiddleDown()) {
            m_mouse_capture.middle = true;
            m_mouse_capture.parent = &parent;
        }
        else if (evt.RightDown()) {
            m_mouse_capture.right = true;
            m_mouse_capture.parent = &parent;
            processed = true;
            if (item_id != -2 && !m_items[item_id]->is_separator() && !m_items[item_id]->is_disabled() &&
                (m_pressed_toggable_id == -1 || m_items[item_id]->get_last_action_type() == GLToolbarItem::Right)) {
                // mouse is inside an icon
                do_action(GLToolbarItem::Right, item_id, parent, true);
                parent.set_as_dirty();
            }
        }
    }

    return processed;
}

void GLToolbar::calc_layout()
{
    switch (m_layout.type)
    {
    default:
    case Layout::Horizontal:
    {
        m_layout.width = get_width_horizontal();
        m_layout.height = get_height_horizontal();
        break;
    }
    case Layout::Vertical:
    {
        m_layout.width = get_width_vertical();
        m_layout.height = get_height_vertical();
        break;
    }
    }

    m_layout.dirty = false;
}

float GLToolbar::get_width_horizontal() const
{
    return get_main_size();
}

float GLToolbar::get_width_vertical() const
{
    return (2.0f * (m_layout.border + m_layout.gap_size)) + m_layout.icons_size;
}

float GLToolbar::get_height_horizontal() const
{
    return (2.0f * (m_layout.border + m_layout.gap_size)) + m_layout.icons_size;
}

float GLToolbar::get_height_vertical() const
{
    return get_main_size();
}

float GLToolbar::get_main_size() const
{
    float size = 2.0f * m_layout.border;
    for (unsigned int i = 0; i < (unsigned int)m_items.size(); ++i)
    {
        if (!m_items[i]->is_visible())
            continue;

        if (m_items[i]->is_separator())
            size += m_layout.separator_size + m_layout.gap_size;
        else
            size += (float)m_layout.icons_size + 2.f * m_layout.gap_size;
    }

    return size;
}

int GLToolbar::get_visible_items_cnt() const
{
    int cnt = 0;
    for (unsigned int i = 0; i < (unsigned int)m_items.size(); ++i)
        if (m_items[i]->is_visible() && !m_items[i]->is_separator())
            cnt++;

    return cnt;
}

void GLToolbar::do_action(GLToolbarItem::EActionType type, int item_id, GLCanvas3D& parent, bool check_hover)
{
    if (m_pressed_toggable_id == -1 || m_pressed_toggable_id == item_id) {
        if (0 <= item_id && item_id < (int)m_items.size()) {
            GLToolbarItem* item = m_items[item_id];
            if (item != nullptr && !item->is_separator() && !item->is_disabled() && (!check_hover || item->is_hovered())) {
                if ((type == GLToolbarItem::Right && item->is_right_toggable()) ||
                    (type == GLToolbarItem::Left && item->is_left_toggable())) {
                    GLToolbarItem::EState state = item->get_state();
                    if (state == GLToolbarItem::Hover)
                        item->set_state(GLToolbarItem::HoverPressed);
                    else if (state == GLToolbarItem::HoverPressed)
                        item->set_state(GLToolbarItem::Hover);
                    else if (state == GLToolbarItem::Pressed)
                        item->set_state(GLToolbarItem::Normal);
                    else if (state == GLToolbarItem::Normal)
                        item->set_state(GLToolbarItem::Pressed);

                    m_pressed_toggable_id = item->is_pressed() ? item_id : -1;
                    item->reset_last_action_type();

                    parent.render();
                    switch (type)
                    {
                    default:
                    case GLToolbarItem::Left:  { item->do_left_action(); break; }
                    case GLToolbarItem::Right: { item->do_right_action(); break; }
                    }
                }
                else {
                    if (m_type == Radio)
                        select_item(item->get_name());
                    else
                        item->set_state(item->is_hovered() ? GLToolbarItem::HoverPressed : GLToolbarItem::Pressed);

                    item->reset_last_action_type();
                    parent.render();
                    switch (type)
                    {
                    default:
                    case GLToolbarItem::Left: { item->do_left_action(); break; }
                    case GLToolbarItem::Right: { item->do_right_action(); break; }
                    }

                    if (m_type == Normal && item->get_state() != GLToolbarItem::Disabled) {
                        // the item may get disabled during the action, if not, set it back to hover state
                        item->set_state(GLToolbarItem::Hover);
                        parent.render();
                    }
                }
            }
        }
    }
}

void GLToolbar::update_hover_state(const Vec2d& mouse_pos, GLCanvas3D& parent)
{
    if (!m_enabled)
        return;

    switch (m_layout.type)
    {
    default:
    case Layout::Horizontal: { update_hover_state_horizontal(mouse_pos, parent); break; }
    case Layout::Vertical:   { update_hover_state_vertical(mouse_pos, parent); break; }
    }
}

void GLToolbar::update_hover_state_horizontal(const Vec2d& mouse_pos, GLCanvas3D& parent)
{
    const Size cnv_size = parent.get_canvas_size();
    const Vec2d scaled_mouse_pos((mouse_pos.x() - 0.5 * (double)cnv_size.get_width()), (0.5 * (double)cnv_size.get_height() - mouse_pos.y()));

    const float icons_size = m_layout.icons_size;
    const float separator_size = m_layout.separator_size;
    const float gap_size = m_layout.gap_size;
    const float border = m_layout.border;

    const float separator_stride = separator_size + gap_size;
    const float icon_stride = icons_size + 2 * gap_size;

    float left = m_layout.left + border;
    float top  = m_layout.top - border - gap_size;

    for (GLToolbarItem* item : m_items) {
        if (!item->is_visible())
            continue;

        if (item->is_separator())
            left += separator_stride;
        else {
            const float right = left + icons_size;
            const float bottom = top - icons_size;

            const GLToolbarItem::EState state = item->get_state();
            bool inside = (left <= (float)scaled_mouse_pos.x()) &&
                          ((float)scaled_mouse_pos.x() <= right) &&
                          (bottom <= (float)scaled_mouse_pos.y()) &&
                          ((float)scaled_mouse_pos.y() <= top);

            switch (state)
            {
            case GLToolbarItem::Normal:
            {
                if (inside) {
                    item->set_state(GLToolbarItem::Hover);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::Hover:
            {
                if (!inside) {
                    item->set_state(GLToolbarItem::Normal);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::Pressed:
            {
                if (inside) {
                    item->set_state(GLToolbarItem::HoverPressed);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::HoverPressed:
            {
                if (!inside) {
                    item->set_state(GLToolbarItem::Pressed);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::Disabled:
            {
                if (inside) {
                    item->set_state(GLToolbarItem::HoverDisabled);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::HoverDisabled:
            {
                if (!inside) {
                    item->set_state(GLToolbarItem::Disabled);
                    parent.set_as_dirty();
                }

                break;
            }
            default:
            {
                break;
            }
            }

            left += icon_stride;
        }
    }
}

void GLToolbar::update_hover_state_vertical(const Vec2d& mouse_pos, GLCanvas3D& parent)
{
    const Size cnv_size = parent.get_canvas_size();
    const Vec2d scaled_mouse_pos((mouse_pos.x() - 0.5 * (double)cnv_size.get_width()), (0.5 * (double)cnv_size.get_height() - mouse_pos.y()));

    const float icons_size = m_layout.icons_size;
    const float separator_size = m_layout.separator_size;
    const float gap_size = m_layout.gap_size;
    const float border = m_layout.border;

    const float separator_stride = separator_size + gap_size;
    const float icon_stride = icons_size + gap_size;

    float left = m_layout.left + border + gap_size;
    float top  = m_layout.top - border - gap_size;

    for (GLToolbarItem* item : m_items) {
        if (!item->is_visible())
            continue;

        if (item->is_separator())
            top -= separator_stride;
        else {
            const float right  = left + icons_size;
            const float bottom = top - icons_size;

            GLToolbarItem::EState state = item->get_state();
            const bool inside = (left <= (float)scaled_mouse_pos.x()) &&
                                ((float)scaled_mouse_pos.x() <= right) &&
                                (bottom <= (float)scaled_mouse_pos.y()) &&
                                ((float)scaled_mouse_pos.y() <= top);

            switch (state)
            {
            case GLToolbarItem::Normal:
            {
                if (inside) {
                    item->set_state(GLToolbarItem::Hover);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::Hover:
            {
                if (!inside) {
                    item->set_state(GLToolbarItem::Normal);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::Pressed:
            {
                if (inside) {
                    item->set_state(GLToolbarItem::HoverPressed);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::HoverPressed:
            {
                if (!inside) {
                    item->set_state(GLToolbarItem::Pressed);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::Disabled:
            {
                if (inside) {
                    item->set_state(GLToolbarItem::HoverDisabled);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::HoverDisabled:
            {
                if (!inside) {
                    item->set_state(GLToolbarItem::Disabled);
                    parent.set_as_dirty();
                }

                break;
            }
            default:
            {
                break;
            }
            }

            top -= icon_stride;
        }
    }
}

GLToolbarItem* GLToolbar::get_item(const std::string& item_name)
{
    if (!m_enabled)
        return nullptr;

    for (GLToolbarItem* item : m_items) {
        if (item->get_name() == item_name)
            return item;
    }
    return nullptr;
}

int GLToolbar::contains_mouse(const Vec2d& mouse_pos, const GLCanvas3D& parent) const
{
    if (!m_enabled)
        return -1;

    switch (m_layout.type)
    {
    default:
    case Layout::Horizontal: { return contains_mouse_horizontal(mouse_pos, parent); }
    case Layout::Vertical:   { return contains_mouse_vertical(mouse_pos, parent); }
    }
}

int GLToolbar::contains_mouse_horizontal(const Vec2d& mouse_pos, const GLCanvas3D& parent) const
{
    const Size cnv_size = parent.get_canvas_size();
    const Vec2d scaled_mouse_pos((mouse_pos.x() - 0.5 * (double)cnv_size.get_width()), (0.5 * (double)cnv_size.get_height() - mouse_pos.y()));

    const float icons_size = m_layout.icons_size;
    const float separator_size = m_layout.separator_size;
    const float gap_size = m_layout.gap_size;
    const float border = m_layout.border;

    float left = m_layout.left + border + gap_size;
    const float top  = m_layout.top - border - gap_size;

    for (size_t id = 0; id < m_items.size(); ++id) {
        GLToolbarItem* item = m_items[id];

        if (!item->is_visible())
            continue;

        if (item->is_separator()) {
            float right = left + separator_size;
            const float bottom = top - icons_size;

            // mouse inside the separator
            if (left <= (float)scaled_mouse_pos.x() &&
                (float)scaled_mouse_pos.x() <= right &&
                bottom <= (float)scaled_mouse_pos.y() &&
                (float)scaled_mouse_pos.y() <= top)
                return id;

            left = right;
            right += gap_size;

            if (id < m_items.size() - 1) {
                // mouse inside the gap
                if (left <= (float)scaled_mouse_pos.x() &&
                    (float)scaled_mouse_pos.x() <= right &&
                    bottom <= (float)scaled_mouse_pos.y() &&
                    (float)scaled_mouse_pos.y() <= top)
                    return -2;
            }

            left = right;
        }
        else {
            float right = left + icons_size;
            const float bottom = top - icons_size;

            // mouse inside the icon
            if (left <= (float)scaled_mouse_pos.x() &&
                (float)scaled_mouse_pos.x() <= right &&
                bottom <= (float)scaled_mouse_pos.y() &&
                (float)scaled_mouse_pos.y() <= top)
                return id;

            left = right;
            right += gap_size;

            if (id < m_items.size() - 1) {
                // mouse inside the gap
                if (left <= (float)scaled_mouse_pos.x() &&
                    (float)scaled_mouse_pos.x() <= right &&
                    bottom <= (float)scaled_mouse_pos.y() &&
                    (float)scaled_mouse_pos.y() <= top)
                    return -2;
            }

            left = right + gap_size;
        }
    }

    return -1;
}

int GLToolbar::contains_mouse_vertical(const Vec2d& mouse_pos, const GLCanvas3D& parent) const
{
    const Size cnv_size = parent.get_canvas_size();
    const Vec2d scaled_mouse_pos((mouse_pos.x() - 0.5 * (double)cnv_size.get_width()), (0.5 * (double)cnv_size.get_height() - mouse_pos.y()));

    const float icons_size = m_layout.icons_size;
    const float separator_size = m_layout.separator_size;
    const float gap_size = m_layout.gap_size;
    const float border = m_layout.border;

    const float left = m_layout.left + border;
    float top = m_layout.top - border;

    for (size_t id = 0; id < m_items.size(); ++id) {
        GLToolbarItem* item = m_items[id];

        if (!item->is_visible())
            continue;

        if (item->is_separator()) {
            const float right = left + icons_size;
            float bottom = top - separator_size;

            // mouse inside the separator
            if (left <= (float)scaled_mouse_pos.x() &&
                (float)scaled_mouse_pos.x() <= right &&
                bottom <= (float)scaled_mouse_pos.y() &&
                (float)scaled_mouse_pos.y() <= top)
                return id;

            top = bottom;
            bottom -= gap_size;

            if (id < m_items.size() - 1) {
                // mouse inside the gap
                if (left <= (float)scaled_mouse_pos.x() &&
                    (float)scaled_mouse_pos.x() <= right &&
                    bottom <= (float)scaled_mouse_pos.y() &&
                    (float)scaled_mouse_pos.y() <= top)
                    return -2;
            }

            top = bottom;
        }
        else {
            const float right = left + icons_size;
            float bottom = top - icons_size;

            // mouse inside the icon
            if (left <= (float)scaled_mouse_pos.x() &&
                (float)scaled_mouse_pos.x() <= right &&
                bottom <= (float)scaled_mouse_pos.y() &&
                (float)scaled_mouse_pos.y() <= top)
                return id;

            top = bottom;
            bottom -= gap_size;

            if (id < m_items.size() - 1) {
                // mouse inside the gap
                if (left <= (float)scaled_mouse_pos.x() &&
                    (float)scaled_mouse_pos.x() <= right &&
                    bottom <= (float)scaled_mouse_pos.y() &&
                    (float)scaled_mouse_pos.y() <= top)
                    return -2;
            }

            top = bottom;
        }
    }

    return -1;
}

void GLToolbar::render_background(float left, float top, float right, float bottom, float border_w, float border_h) const
{
    const unsigned int tex_id = m_background_texture.texture.get_id();
    const float tex_width = (float)m_background_texture.texture.get_width();
    const float tex_height = (float)m_background_texture.texture.get_height();
    if (tex_id != 0 && tex_width > 0.0f && tex_height > 0.0f) {
        const float inv_tex_width  = 1.0f / tex_width;
        const float inv_tex_height = 1.0f / tex_height;

        const float internal_left   = left + border_w;
        const float internal_right  = right - border_w;
        const float internal_top    = top - border_h;
        const float internal_bottom = bottom + border_w;

        const float left_uv   = 0.0f;
        const float right_uv  = 1.0f;
        const float top_uv    = 1.0f;
        const float bottom_uv = 0.0f;

        const float internal_left_uv = (float)m_background_texture.metadata.left * inv_tex_width;
        const float internal_right_uv = 1.0f - (float)m_background_texture.metadata.right * inv_tex_width;
        const float internal_top_uv = 1.0f - (float)m_background_texture.metadata.top * inv_tex_height;
        const float internal_bottom_uv = (float)m_background_texture.metadata.bottom * inv_tex_height;

        // top-left corner
        if (m_layout.horizontal_orientation == Layout::HO_Left || m_layout.vertical_orientation == Layout::VO_Top)
            GLTexture::render_sub_texture(tex_id, left, internal_left, internal_top, top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, left, internal_left, internal_top, top, { { left_uv, internal_top_uv }, { internal_left_uv, internal_top_uv }, { internal_left_uv, top_uv }, { left_uv, top_uv } });

        // top edge
        if (m_layout.vertical_orientation == Layout::VO_Top)
            GLTexture::render_sub_texture(tex_id, internal_left, internal_right, internal_top, top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, internal_left, internal_right, internal_top, top, { { internal_left_uv, internal_top_uv }, { internal_right_uv, internal_top_uv }, { internal_right_uv, top_uv }, { internal_left_uv, top_uv } });

        // top-right corner
        if (m_layout.horizontal_orientation == Layout::HO_Right || m_layout.vertical_orientation == Layout::VO_Top)
            GLTexture::render_sub_texture(tex_id, internal_right, right, internal_top, top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, internal_right, right, internal_top, top, { { internal_right_uv, internal_top_uv }, { right_uv, internal_top_uv }, { right_uv, top_uv }, { internal_right_uv, top_uv } });

        // center-left edge
        if (m_layout.horizontal_orientation == Layout::HO_Left)
            GLTexture::render_sub_texture(tex_id, left, internal_left, internal_bottom, internal_top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, left, internal_left, internal_bottom, internal_top, { { left_uv, internal_bottom_uv }, { internal_left_uv, internal_bottom_uv }, { internal_left_uv, internal_top_uv }, { left_uv, internal_top_uv } });

        // center
        GLTexture::render_sub_texture(tex_id, internal_left, internal_right, internal_bottom, internal_top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });

        // center-right edge
        if (m_layout.horizontal_orientation == Layout::HO_Right)
            GLTexture::render_sub_texture(tex_id, internal_right, right, internal_bottom, internal_top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, internal_right, right, internal_bottom, internal_top, { { internal_right_uv, internal_bottom_uv }, { right_uv, internal_bottom_uv }, { right_uv, internal_top_uv }, { internal_right_uv, internal_top_uv } });

        // bottom-left corner
        if (m_layout.horizontal_orientation == Layout::HO_Left || m_layout.vertical_orientation == Layout::VO_Bottom)
            GLTexture::render_sub_texture(tex_id, left, internal_left, bottom, internal_bottom, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, left, internal_left, bottom, internal_bottom, { { left_uv, bottom_uv }, { internal_left_uv, bottom_uv }, { internal_left_uv, internal_bottom_uv }, { left_uv, internal_bottom_uv } });

        // bottom edge
        if (m_layout.vertical_orientation == Layout::VO_Bottom)
            GLTexture::render_sub_texture(tex_id, internal_left, internal_right, bottom, internal_bottom, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, internal_left, internal_right, bottom, internal_bottom, { { internal_left_uv, bottom_uv }, { internal_right_uv, bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_left_uv, internal_bottom_uv } });

        // bottom-right corner
        if (m_layout.horizontal_orientation == Layout::HO_Right || m_layout.vertical_orientation == Layout::VO_Bottom)
            GLTexture::render_sub_texture(tex_id, internal_right, right, bottom, internal_bottom, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, internal_right, right, bottom, internal_bottom, { { internal_right_uv, bottom_uv }, { right_uv, bottom_uv }, { right_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv } });
    }
}

void GLToolbar::render_arrow(const GLCanvas3D& parent, GLToolbarItem* highlighted_item)
{
    // arrow texture not initialized
    if (m_arrow_texture.get_id() == 0)
        return;

    const Size cnv_size = parent.get_canvas_size();
    const float cnv_w = (float)cnv_size.get_width();
    const float cnv_h = (float)cnv_size.get_height();

    if (cnv_w == 0 || cnv_h == 0)
        return;

    const float inv_cnv_w = 1.0f / cnv_w;
    const float inv_cnv_h = 1.0f / cnv_h;

    const float icons_size_x = 2.0f * m_layout.icons_size * inv_cnv_w;
    const float icons_size_y = 2.0f * m_layout.icons_size * inv_cnv_h;
    const float separator_size = 2.0f * m_layout.separator_size * inv_cnv_w;
    const float gap_size = 2.0f * m_layout.gap_size * inv_cnv_w;
    const float border_x = 2.0f * m_layout.border * inv_cnv_w;
    const float border_y = 2.0f * m_layout.border * inv_cnv_h;

    const float separator_stride = separator_size + gap_size;
    const float icon_stride = icons_size_x + gap_size;

    float left = 2.0f * m_layout.left * inv_cnv_w + border_x;
    float top = 2.0f * m_layout.top * inv_cnv_h - 2.0f * border_y - icons_size_y;

    bool found = false;
    for (const GLToolbarItem* item : m_items) {
        if (!item->is_visible())
            continue;

        if (item->is_separator())
            left += separator_stride;
        else {
            if (item->get_name() == highlighted_item->get_name()) {
                found = true;
                break;
            }
            left += icon_stride;
        }
    }
    if (!found)
        return;

    const float right = left + icons_size_x;

    const unsigned int tex_id = m_arrow_texture.get_id();
    // arrow width and height
    const float arr_tex_width = (float)m_arrow_texture.get_width();
    const float arr_tex_height = (float)m_arrow_texture.get_height();
    if (tex_id != 0 && arr_tex_width > 0.0f && arr_tex_height > 0.0f) {
        const float arrow_size_x = 2.0f * arr_tex_width * inv_cnv_w;
        const float arrow_size_y = 2.0f * arr_tex_height * inv_cnv_h;

        const float left_uv   = 0.0f;
        const float right_uv  = 1.0f;
        const float top_uv    = 1.0f;
        const float bottom_uv = 0.0f;

        top -= border_y;
        const float bottom = top - arrow_size_y * icons_size_x / arrow_size_x;

        GLTexture::render_sub_texture(tex_id, left, right, bottom, top, { { left_uv, top_uv }, { right_uv, top_uv }, { right_uv, bottom_uv }, { left_uv, bottom_uv } });
    }
}

void GLToolbar::render_horizontal(const GLCanvas3D& parent)
{
    const Size cnv_size = parent.get_canvas_size();
    const float cnv_w = (float)cnv_size.get_width();
    const float cnv_h = (float)cnv_size.get_height();

    if (cnv_w == 0 || cnv_h == 0)
        return;

    const unsigned int tex_id = m_icons_texture.get_id();
    const int tex_width = m_icons_texture.get_width();
    const int tex_height = m_icons_texture.get_height();

    if (tex_id == 0 || tex_width <= 0 || tex_height <= 0)
        return;

    const float inv_cnv_w = 1.0f / cnv_w;
    const float inv_cnv_h = 1.0f / cnv_h;

    const float icons_size_x = 2.0f * m_layout.icons_size * inv_cnv_w;
    const float icons_size_y = 2.0f * m_layout.icons_size * inv_cnv_h;
    const float separator_size = 2.0f * m_layout.separator_size * inv_cnv_w;
    const float gap_size_x = 2.0f * m_layout.gap_size * inv_cnv_w;
    const float gap_size_y = 2.0f * m_layout.gap_size * inv_cnv_h;
    const float border_w = 2.0f * m_layout.border * inv_cnv_w;
    const float border_h = 2.0f * m_layout.border * inv_cnv_h;
    const float width = 2.0f * get_width() * inv_cnv_w;
    const float height = 2.0f * get_height() * inv_cnv_h;

    const float separator_stride = separator_size + gap_size_x;
    const float icon_stride = icons_size_x + 2 * gap_size_x;

    float left   = 2.0f * m_layout.left * inv_cnv_w;
    float top    = 2.0f * m_layout.top * inv_cnv_h;
    const float right  = left + width;
    const float bottom = top - height;

    render_background(left, top, right, bottom, border_w, border_h);

    const float margin_w = border_w + gap_size_x;
    const float margin_h = border_h + gap_size_y;

    left += margin_w;
    top  -= margin_h;
            
    // renders icons
    int id = 0;
    for (const GLToolbarItem* item : m_items) {
        id++;
        if (!item->is_visible())
            continue;

        if (item->is_separator())
            left += separator_stride;
        else {
            if (item->is_pressed())
                render_background(left - (id == m_items.size() ? border_w : margin_w),
                                  top + margin_h, 
                                  left + icons_size_x + (id == 1 ? border_w : margin_w), 
                                  top - icons_size_y - margin_h, 
                                  border_w, border_h);

            item->render(parent, tex_id, left, left + icons_size_x, top - icons_size_y, top, (unsigned int)tex_width, (unsigned int)tex_height, (unsigned int)(m_layout.icons_size));
            left += icon_stride;
        }
    }
}

void GLToolbar::render_vertical(const GLCanvas3D& parent)
{
    const Size cnv_size = parent.get_canvas_size();
    const float cnv_w = (float)cnv_size.get_width();
    const float cnv_h = (float)cnv_size.get_height();

    if (cnv_w == 0 || cnv_h == 0)
        return;

    const unsigned int tex_id = m_icons_texture.get_id();
    const int tex_width = m_icons_texture.get_width();
    const int tex_height = m_icons_texture.get_height();

    if (tex_id == 0 || tex_width <= 0 || tex_height <= 0)
        return;

    const float inv_cnv_w = 1.0f / cnv_w;
    const float inv_cnv_h = 1.0f / cnv_h;

    const float icons_size_x = 2.0f * m_layout.icons_size * inv_cnv_w;
    const float icons_size_y = 2.0f * m_layout.icons_size * inv_cnv_h;
    const float separator_size = 2.0f * m_layout.separator_size * inv_cnv_h;
    const float gap_size_x = 2.0f * m_layout.gap_size * inv_cnv_w;
    const float gap_size_y = 2.0f * m_layout.gap_size * inv_cnv_h;
    const float border_w = 2.0f * m_layout.border * inv_cnv_w;
    const float border_h = 2.0f * m_layout.border * inv_cnv_h;
    const float width = 2.0f * get_width() * inv_cnv_w;
    const float height = 2.0f * get_height() * inv_cnv_h;

    const float separator_stride = separator_size + gap_size_y;
    const float icon_stride = icons_size_y + 2 * gap_size_y;

    float left         = 2.0f * m_layout.left * inv_cnv_w;
    float top          = 2.0f * m_layout.top * inv_cnv_h;
    const float right  = left + width;
    const float bottom = top - height;

    render_background(left, top, right, bottom, border_w, border_h);

    left += border_w + gap_size_x;
    top  -= border_h + gap_size_y;

    // renders icons
    for (const GLToolbarItem* item : m_items) {
        if (!item->is_visible())
            continue;

        if (item->is_separator())
            top -= separator_stride;
        else {
            item->render(parent, tex_id, left, left + icons_size_x, top - icons_size_y, top, (unsigned int)tex_width, (unsigned int)tex_height, (unsigned int)(m_layout.icons_size));
            top -= icon_stride;
        }
    }
}

bool GLToolbar::generate_icons_texture()
{
    std::string path = resources_dir() + "/icons/";
    std::vector<std::string> filenames;
    for (GLToolbarItem* item : m_items) {
        const std::string& icon_filename = item->get_icon_filename();
        if (!icon_filename.empty())
            filenames.push_back(path + icon_filename);
    }

    std::vector<std::pair<int, bool>> states;
    if (m_type == Normal) {
        states.push_back({ 1, false }); // Normal
        states.push_back({ 0, false }); // Pressed
        states.push_back({ 2, false }); // Disabled
        states.push_back({ 0, false }); // Hover
        states.push_back({ 0, false }); // HoverPressed
        states.push_back({ 2, false }); // HoverDisabled
        states.push_back({ 0, false }); // HighlightedShown
        states.push_back({ 2, false }); // HighlightedHidden
    }
    else {
        states.push_back({ 1, false }); // Normal
        states.push_back({ 1, true });  // Pressed
        states.push_back({ 1, false }); // Disabled
        states.push_back({ 0, false }); // Hover
        states.push_back({ 1, true });  // HoverPressed
        states.push_back({ 1, false }); // HoverDisabled
        states.push_back({ 0, false }); // HighlightedShown
        states.push_back({ 1, false }); // HighlightedHidden
    }

    unsigned int sprite_size_px = (unsigned int)(m_layout.icons_size);
//    // force even size
//    if (sprite_size_px % 2 != 0)
//        sprite_size_px += 1;

    bool res = m_icons_texture.load_from_svg_files_as_sprites_array(filenames, states, sprite_size_px, false);
    if (res)
        m_icons_texture_dirty = false;

    return res;
}

bool GLToolbar::update_items_visibility()
{
    bool ret = false;

    for (GLToolbarItem* item : m_items) {
        ret |= item->update_visibility();
    }

    if (ret)
        m_layout.dirty = true;

    // updates separators visibility to avoid having two of them consecutive
    bool any_item_visible = false;
    for (GLToolbarItem* item : m_items) {
        if (!item->is_separator())
            any_item_visible |= item->is_visible();
        else {
            item->set_visible(any_item_visible);
            any_item_visible = false;
        }
    }

    return ret;
}

bool GLToolbar::update_items_enabled_state()
{
    bool ret = false;

    for (int i = 0; i < (int)m_items.size(); ++i)
    {
        GLToolbarItem* item = m_items[i];
        ret |= item->update_enabled_state();
        if (item->is_enabled() && (m_pressed_toggable_id != -1) && (m_pressed_toggable_id != i))
        {
            ret = true;
            item->set_state(GLToolbarItem::Disabled);
        }
    }

    if (ret)
        m_layout.dirty = true;

    return ret;
}

} // namespace GUI
} // namespace Slic3r
