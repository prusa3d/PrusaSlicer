#include "Slic3r/App/Render/Types.hpp"
#include "Slic3r/App/Render/ScreenInfo.hpp"

namespace Slic3r::App::Render {

Rect Rect::from(int x, int y, const ScreenInfo& screen)
{
    return {
        x, y, static_cast<int>(screen.physical_width()) - x, static_cast<int>(screen.physical_height()) - y
    };
}


} // namespace Slic3r::App::Render
