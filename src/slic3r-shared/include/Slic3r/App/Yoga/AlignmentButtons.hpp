#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/RevertableControl.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/Domain/TextConfiguration.hpp"

namespace Slic3r::App::Yoga {

class AlignmentButtons : public Item, public RevertableControl
{
public:
    explicit AlignmentButtons();
    virtual ~AlignmentButtons();

    struct Callbacks
    {
        std::function<void(const Domain::FontProp::Align& align)> align_changed{nullptr};
    };

    Callbacks& callbacks();

    const Domain::FontProp::Align& align() const;
    void set_align(const Domain::FontProp::Align& align);

    void set_default(const Domain::FontProp::Align& default_align);
    bool is_changed_value() const override;
    void reset() override;

private:
    Yoga::LayoutButton* m_left_align_btn{nullptr};
    Yoga::LayoutButton* m_right_align_btn{nullptr};
    Yoga::LayoutButton* m_center_align_btn{nullptr};
    Yoga::ButtonGroup m_group_horizontal_align;
    Yoga::LayoutButton* m_top_align_btn{nullptr};
    Yoga::LayoutButton* m_bottom_align_btn{nullptr};
    Yoga::LayoutButton* m_middle_align_btn{nullptr};
    Yoga::ButtonGroup m_group_vertical_align;

    Callbacks m_callbacks;
    Domain::FontProp::Align m_align;
    Domain::FontProp::Align m_default_align;
};

} // namespace Slic3r::App::Yoga
