#include "Slic3r/App/ResourceResolver.hpp"

#include <optional>

#include <Slic3r/Log.hpp>
#include <boost/filesystem/operations.hpp>

namespace Slic3r::App {

ResourceResolver::ResourceResolver(const std::string& resource_path, const std::string& data_path)
    : m_resources_path(resource_path), m_data_path(data_path), m_alt_paths{{"/presets/", "presets/", "presets/local/"}}
{}

std::string ResourceResolver::resolve(const std::string& relative_filepath)
{
    auto check_path = [](const boost::filesystem::path& root, const std::string& stem) -> std::optional<boost::filesystem::path>
    {
        boost::filesystem::path p = root / stem;
        if (boost::filesystem::exists(p)) {
            return p;
        }
        return std::nullopt;

    };

    for (const auto& [prefix, resource_path_replacement, data_path_replacement] : m_alt_paths) {
        if (!relative_filepath.starts_with(prefix)) {
            continue;
        }
        std::string base =  relative_filepath.substr(prefix.size());
        auto p           = check_path(m_data_path, data_path_replacement + base);
        if (p) {
            return p->string();
        }

        p = check_path(m_resources_path, resource_path_replacement + base);
        if (p) {
            return p->string();
        }
    }

    boost::filesystem::path path = boost::filesystem::exists(relative_filepath)
        ? relative_filepath
        : (m_resources_path / relative_filepath);

    return path.string();
}

} // namespace Slic3r::App
