#include "Slic3r/App/Plater/History.hpp"

#include "Slic3r/App/Yoga/Text.hpp"

namespace Slic3r::App::Plater {

History::History() : Window("history")
{
    set_min_width(330);
    emplace_back<Yoga::Text>("Action history");
}

} // namespace Slic3r::App::Plater
