
#include "Slic3r/App/Yoga/AlignmentButtons.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/LegacyFormat.hpp"

using namespace Slic3r::Biz;

namespace Slic3r::App::Yoga {

static LayoutButton* add_button(Item* parent, Render::Icon icon, const std::string& tooltip)
{
    LayoutButton* btn = parent->emplace_back<LayoutButton>("", icon, tooltip);
    btn->set_checkable(true);
    btn->set_min_width(24.f);
    btn->set_min_height(24.f);
    return btn;
}

AlignmentButtons::AlignmentButtons()
{
    set_gap(10);

    m_left_align_btn   = add_button(this, Render::Icon::AlignHLeftBtn, _u8L("Left"));
    m_center_align_btn = add_button(this, Render::Icon::AlignHCenterBtn, _u8L("Center"));
    m_right_align_btn  = add_button(this, Render::Icon::AlignHRightBtn, _u8L("Right"));

    m_group_horizontal_align.set_buttons({m_left_align_btn, m_center_align_btn, m_right_align_btn});
    m_group_horizontal_align.callbacks().action = [this](AbstractButton* btn) {
        m_align.horizontal = btn == m_left_align_btn ? Domain::FontProp::HorizontalAlign::left :
            btn == m_right_align_btn                 ? Domain::FontProp::HorizontalAlign::right :
                                                       Domain::FontProp::HorizontalAlign::center;
        if (m_callbacks.align_changed)
            m_callbacks.align_changed(m_align);
        update_revert_button();
    };

    m_top_align_btn    = add_button(this, Render::Icon::AlignVTopBtn, _u8L("Top"));
    m_middle_align_btn = add_button(this, Render::Icon::AlignVCenterBtn, _u8L("Middle"));
    m_bottom_align_btn = add_button(this, Render::Icon::AlignVBottomBtn, _u8L("Bottom"));

    m_group_vertical_align.set_buttons({m_top_align_btn, m_middle_align_btn, m_bottom_align_btn});
    m_group_vertical_align.callbacks().action = [this](AbstractButton* btn) {
        m_align.vertical = btn == m_top_align_btn ? Domain::FontProp::VerticalAlign::top :
            btn == m_bottom_align_btn             ? Domain::FontProp::VerticalAlign::bottom :
                                                    Domain::FontProp::VerticalAlign::center;
        if (m_callbacks.align_changed)
            m_callbacks.align_changed(m_align);
        update_revert_button();
    };
}

AlignmentButtons::~AlignmentButtons() {}

AlignmentButtons::Callbacks& AlignmentButtons::callbacks()
{
    return m_callbacks;
}

const Domain::FontProp::Align& AlignmentButtons::align() const
{
    return m_align;
}

void AlignmentButtons::set_align(const Domain::FontProp::Align& align)
{
    m_align = align;
    update_revert_button();

    switch (m_align.horizontal) {
    case Domain::FontProp::HorizontalAlign::left:
        m_left_align_btn->set_checked(true);
        break;
    case Domain::FontProp::HorizontalAlign::center:
        m_center_align_btn->set_checked(true);
        break;
    case Domain::FontProp::HorizontalAlign::right:
        m_right_align_btn->set_checked(true);
        break;
    }

    switch (m_align.vertical) {
    case Domain::FontProp::VerticalAlign::top:
        m_top_align_btn->set_checked(true);
        break;
    case Domain::FontProp::VerticalAlign::center:
        m_middle_align_btn->set_checked(true);
        break;
    case Domain::FontProp::VerticalAlign::bottom:
        m_bottom_align_btn->set_checked(true);
        break;
    }
}

void AlignmentButtons::set_default(const Domain::FontProp::Align& default_align)
{
    m_default_align = default_align;
    update_revert_button();
}

bool AlignmentButtons::is_changed_value() const
{
    return m_align.horizontal != m_default_align.horizontal
        || m_align.vertical != m_default_align.vertical;
}

void AlignmentButtons::reset()
{
    set_align(m_default_align);
    if (m_callbacks.align_changed) {
        m_callbacks.align_changed(m_align);
    }
}

} // namespace Slic3r::App::Yoga
