#pragma once

#include "Slic3r/App/Render/ImguiTypes.hpp"
#include "Slic3r/App/Render/Types.hpp"

#include <imgui/imgui.h>

#include <map>

namespace Slic3r::App::Render {

using ImguiFonts = std::map<ImguiFontType, ImFont*>;

class ImguiFontHelper
{
public:
    ImguiFontHelper();

    ImFont* font(Render::ImguiFontType type);

private:
    void create_font_texture();

private:
    TexturePtr m_font_texture;
    ImguiFonts m_fonts;
};

} // namespace Slic3r::App::Render
