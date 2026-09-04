#pragma once

#include <Slic3r/App/Render/IResourceResolver.hpp>

#include <boost/filesystem/path.hpp>

namespace Slic3r::App {

class ResourceResolver : public Render::IResourceResolver
{
public:
    ResourceResolver(const std::string& resource_path, const std::string& data_path);

    std::string resolve(const std::string& relative_filepath) override;

private:
    struct AltPath
    {
        std::string prefix;
        std::string resource_path_replacement;
        std::string data_path_replacement;
    };

    boost::filesystem::path m_resources_path;
    boost::filesystem::path m_data_path;

    std::vector<AltPath> m_alt_paths;
};

} // namespace Slic3r::App
