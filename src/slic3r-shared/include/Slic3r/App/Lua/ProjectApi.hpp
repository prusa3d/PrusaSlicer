#pragma once

#include "Slic3r/Directories.hpp"
#include "Slic3r/Biz/Lua/LuaEngine.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Emboss/IFontManager.hpp"
#include "Slic3r/Biz/Emboss/TextPresetManager.hpp"

namespace Slic3r::App::Lua {

class ProjectApi
{
public:
    ProjectApi(
        Biz::ProjectInteractor& project_interactor,
        Biz::Emboss::IFontManager& font_manager
    );

    void register_api(Biz::Lua::LuaEngine& lua);

private:
    Biz::ProjectInteractor& m_project_interactor;
    Biz::Emboss::IFontManager& m_font_manager;
    Biz::Emboss::TextPresetManager m_text_preset_manager;
    Domain::FontList m_fav_fonts;
};

}