#include "Slic3r/App/Wildcards.hpp"
#include "Slic3r/Biz/FileLoadingLogic.hpp"

namespace Slic3r::App::Wildcards {

namespace {

// Build the "All supported files|*.ext1;*.ext2;..." pattern from the canonical
// Biz-layer list.
std::string build_all_import_files_pattern()
{
    std::string globs;
    for (const std::string& ext : Biz::FileLoadingLogic::get_import_extensions()) {
        if (!globs.empty())
            globs += ";";
        globs += "*" + ext;
    }
    return "All supported files|" + globs;
}

const std::map<TypeFlag, std::string>& get_wildcard_map()
{
    // This map is to be used by GUI only (file dialogs). The list of importable files is defined
    // in Biz::FileLoadingLogic::get_import_extensions(), which is the single source of truth.
    static const std::map<TypeFlag, std::string> map = {
        {TypeFlag::GCode,          "G-code files (*.gcode)|*.gcode"},
        {TypeFlag::BinaryGCode,    "Binary G-code files (*.bgcode)|*.bgcode"},
        {TypeFlag::Sl1,            "SL1 files (*.sl1)|*.sl1"},
        {TypeFlag::Sl1S,           "SL1S files (*.sl1s)|*.sl1s"},
        {TypeFlag::Stl,            "STL files (*.stl)|*.stl"},
        {TypeFlag::Obj,            "OBJ files (*.obj)|*.obj"},
        {TypeFlag::Step,           "STEP files (*.step, *.stp)|*.step;*.stp"},
        {TypeFlag::Project3mf,     "3MF files (*.3mf)|*.3mf"},
        {TypeFlag::AllImportFiles, build_all_import_files_pattern()},
        {TypeFlag::Png,            "PNG files (*.png)|*.png"},
        {TypeFlag::Svg,            "SVG files (*.svg)|*.svg"},
        {TypeFlag::Zip,            "Zip files (*.zip)|*.zip"},
        {TypeFlag::AllTextures,    "Texture files|*.png;*.svg"},
    };
    return map;
}

} // namespace

std::string generate_wildcards(TypeFlag all_flags, TypeFlag primary_flag /*= TypeFlag::None*/)
{
    const auto& flag_to_wildcard_map = get_wildcard_map();
    std::string result_string;

    // Concatenate wildcards from map according to flags
    // First is primary
    if (primary_flag != TypeFlag::None) {
        auto it = flag_to_wildcard_map.find(primary_flag);
        ASSERT(it != flag_to_wildcard_map.end());
        result_string += it->second;
        if (all_flags == TypeFlag::None) {
            return result_string;
        }
    }

    // Concatenate rest of the wildcards
    using underlying_type = std::underlying_type_t<TypeFlag>;
    for (underlying_type flag_value = 1;
         flag_value < static_cast<underlying_type>(TypeFlag::AllFlags);
         flag_value <<= 1)
    {
        TypeFlag single_flag = static_cast<TypeFlag>(flag_value);
        if (single_flag == primary_flag) {
            continue;
        }
        if ((all_flags & single_flag) != TypeFlag::None) {
            auto it = flag_to_wildcard_map.find(single_flag);
            ASSERT(it != flag_to_wildcard_map.end());
            if (!result_string.empty()) {
                result_string += "|";
            }
            result_string += it->second;
        }
    }
    return result_string;
}

} // namespace Slic3r::App::Wildcards
