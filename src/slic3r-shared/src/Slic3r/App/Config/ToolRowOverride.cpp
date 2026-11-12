#include "Slic3r/App/Config/ToolRowOverride.hpp"

#include "Slic3r/App/Config/ConfigItemUtils.hpp"
#include "Slic3r/Domain/Config.hpp"

namespace Slic3r::App {

std::string ToolRowOverride::dnd_key() const
{
    return override_item->name();
}

} // namespace Slic3r::App
