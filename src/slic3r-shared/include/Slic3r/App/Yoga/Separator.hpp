#pragma once

#include "Slic3r/App/Yoga/Rectangle.hpp"

namespace Slic3r::App::Yoga {

class Separator : public Rectangle {
public:
    Separator(Orientation orientation = Orientation::Horizontal);

protected:
    Vec2f get_item_size() override;
};

}
