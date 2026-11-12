#include "Slic3r/App/Render/ImguiFontHelper.hpp"

#include "Slic3r/Exception.hpp"

#include <Slic3r/Assert.hpp>
#include <Slic3r/Log.hpp>
#include "Slic3r/Directories.hpp"

#include <imgui/imgui.h>
#include <boost/filesystem/operations.hpp>

#include <array>

namespace Slic3r::App::Render {

ImguiFontHelper::ImguiFontHelper()
{
    create_font_texture();
}

ImFont* ImguiFontHelper::font(Render::ImguiFontType type)
{
    auto it = m_fonts.find(type);
    DEBUG_ASSERT(it != m_fonts.end());
    return (it != m_fonts.end()) ? it->second : nullptr;
}

static ImFont*
load_font(const std::string& base_filename, const std::vector<std::string>& additional_fonts)
{
    ImGuiIO& io            = ImGui::GetIO();
    const std::string path = Slic3r::resources_dir() + "/fonts/" + base_filename;
    ASSERT(boost::filesystem::exists(path), "font file does not exists");

    static const std::array<ImWchar, 3> s_private_use_range{0xE000, 0xEFFF, 0};

    ImFontConfig base_config;
    base_config.GlyphExcludeRanges = s_private_use_range.data();

    ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), 0, &base_config);

    if (font == nullptr) {
        SPDLOG_WARN("Fallback to default font");
        font = io.Fonts->AddFontDefault();
        if (font == nullptr) {
            throw Slic3r::RuntimeError("ImGui: Could not load default font");
        }
    }

    ImFontConfig config;
    config.MergeMode = true;

    for (const std::string& font : std::as_const(additional_fonts)) {
        const std::string font_path = Slic3r::resources_dir() + "/fonts/" + font;
        ASSERT(boost::filesystem::exists(font_path), "additional font file does not exists");
        io.Fonts->AddFontFromFileTTF(font_path.c_str(), 0, &config);
    }

    return font;
}

void ImguiFontHelper::create_font_texture()
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    m_fonts[ImguiFontType::Regular] = load_font(
        "Inter-Regular.ttf",
        {"NotoSansCJK-Regular.ttc", "NotoSansThai-Regular.ttf", "Slic3rIcons.ttf"}
    );
    m_fonts[ImguiFontType::Bold] = load_font(
        "Inter-Bold.ttf",
        {"NotoSansCJK-Bold.ttc", "NotoSansThai-Bold.ttf", "Slic3rIcons.ttf"}
    );
    m_fonts[ImguiFontType::Italic] = load_font(
        "Inter-Italic.ttf",
        {"NotoSansCJK-Italic.ttc", "NotoSansThai-Regular.ttf", "Slic3rIcons.ttf"}
    );
}

} // namespace Slic3r::App::Render
