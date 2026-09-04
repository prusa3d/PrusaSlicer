#include "Slic3r/Biz/Preset/IO/PresetLoader.hpp"
#include "Slic3r/Biz/Yaml/Yaml.hpp"
#include "Slic3r/Biz/Yaml/YamlSlic3rTypes.hpp"

#include <boost/filesystem/directory.hpp>

#include "oneapi/tbb/parallel_for.h"
#include "Slic3r/Biz/Preset/IO/PresetYamlDesc.hpp"

namespace Slic3r::Biz::Preset::IO {

namespace Details {

void collect_names(PresetNamesMapCollection& preset_names, const Domain::Preset::PresetNode& node, Domain::Preset::PresetKind kind, Domain::Preset::PresetOrigin origin, const std::string& root_id)
{
    if (node.name.has_value()) {
        auto& dest = preset_names[kind];

        std::string name{node.short_name().value()};
        if (auto it = dest.find(name); it != dest.end()) {
            it->second.id.insert(node.id);
        } else {
            dest.emplace(name, Domain::Preset::PresetName{name, {node.id}, {root_id}, origin});
        }
    }

    for (const auto& v : node.variants) {
        collect_names(preset_names, v, kind, origin, root_id);
    }
}

Domain::Preset::PresetNamesCollection to_preset_names_collection(const PresetNamesMapCollection& preset_names)
{
    Domain::Preset::PresetNamesCollection names;
    for (const auto& [kind, preset_names_map] : preset_names) {
        Domain::Preset::PresetNames dest_preset_names;

        std::ranges::copy(
            preset_names_map | std::views::values,
            std::back_inserter(dest_preset_names)
        );

        names.emplace(kind, std::move(dest_preset_names));
    }

    return names;
}

} // namespace Details

void PresetLoader::load(const std::string& file_name, std::mutex& mutex, PresetOrigin origin)
{
    Yaml::parse_all_documents_in_file(file_name.c_str(), [this, &mutex, &file_name, origin](const auto& doc) {
        auto preset = Yaml::parse_struct_unwrap<RootPresetNode>(doc);
        std::lock_guard guard(mutex);
        preset.origin = origin;
        if (origin == PresetOrigin::User) {
            preset.user_file = file_name;
        }
        Details::collect_names(m_preset_names, preset, preset.kind, origin, preset.id);
        m_presets[preset.kind].emplace_back(std::move(preset));
    });
}

void PresetLoader::load_from_string(std::string_view source, PresetOrigin origin)
{
    Yaml::parse_all_documents_in_string(source, [this, origin](const auto& doc) {
        auto preset = Yaml::parse_struct_unwrap<RootPresetNode>(doc);
        preset.origin = origin;
        Details::collect_names(m_preset_names, preset, preset.kind, origin, preset.id);
        m_presets[preset.kind].emplace_back(std::move(preset));
    });
}

void PresetLoader::load_dir(const std::string& dir_path, PresetOrigin origin)
{
    // Collect paths to files to load from.
    std::vector<boost::filesystem::path> paths;
    for (const auto& entry : boost::filesystem::directory_iterator{ dir_path }) {
        boost::filesystem::path path = entry.path();
        if (is_regular_file(entry) && path.has_extension() && path.extension() == ".yaml" && path.filename() != "vendor.yaml")
            paths.emplace_back(path);
    }

    // The presets are loaded in parallel and added into m_presets, which is
    // guarded by this mutex.
    std::mutex mutex;
    tbb::parallel_for(tbb::blocked_range<size_t>(0, paths.size()),
        [this, &mutex, &paths, origin](const tbb::blocked_range<size_t> &range) {
            for (size_t i = range.begin(); i < range.end(); ++i)
                load(paths[i].string(), mutex, origin);
        }
    );
}

std::tuple<PresetLoader::PresetCollection, PresetLoader::PresetNamesCollection> PresetLoader::release()
{
    PresetCollection presets = std::move(m_presets);
    PresetNamesCollection names = Details::to_preset_names_collection(m_preset_names);

    m_preset_names.clear();
    m_presets.clear();

    return {presets, names};
}

Domain::Preset::PresetNamesCollection collect_names(
    const Domain::Preset::PresetCollection& presets
)
{
    Details::PresetNamesMapCollection names;
    std::ranges::for_each(
        presets | std::views::values | std::views::join,
        [&names](const auto& node) { Details::collect_names(names, node, node.kind, node.origin, node.id); }
    );

    return Details::to_preset_names_collection(names);
}

} // namespace Slic3r::Biz::Preset::IO
