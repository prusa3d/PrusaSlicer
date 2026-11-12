#pragma once
#include "Slic3r/App/Yoga/LayoutButton.hpp"

namespace Slic3r::App {

class FavoriteButton : public Yoga::LayoutButton
{
public:
    FavoriteButton();
    ~FavoriteButton() = default;

    bool show_only_on_hover() const;
    void set_show_only_on_hover(bool show_only_on_hover);

protected:
    void checked_updated_internal() override;
    void hovered_updated_internal() override;

private:
    bool m_show_only_on_hover{false};
};

} // namespace Slic3r::App
