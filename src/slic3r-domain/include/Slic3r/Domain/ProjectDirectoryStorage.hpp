#pragma once

#include "Slic3r/Log.hpp"

#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>

namespace Slic3r::Domain {
struct ProjectDirectoryStorage {
    
private:
    boost::filesystem::path m_output_dir_path; 
    boost::filesystem::path m_project_dir_path;
    std::string m_output_extension;

    boost::filesystem::path validate_dir_path(const boost::filesystem::path& path) const {
        boost::filesystem::path final_path = path;
        boost::system::error_code ec;
        
        if (!boost::filesystem::exists(path, ec) || boost::filesystem::is_regular_file(path, ec)) {
            final_path = path.parent_path();
        }
        
        if (!boost::filesystem::exists(final_path, ec) || ec) {
            SPDLOG_ERROR("Failed to set path. Target directory check path: {} Error: {}", final_path.string(), ec.message());
            return {};
        }
        return final_path;
    }

public:
    boost::filesystem::path output_dir(const std::string& app_config_val) const
    {
        boost::system::error_code ec;
        boost::filesystem::path app_config_path{app_config_val};
        if (m_output_dir_path.empty()
            && m_project_dir_path.empty()
            && boost::filesystem::exists(app_config_path, ec)
            && boost::filesystem::is_directory(app_config_path, ec))
        {
            return app_config_path;
        }
        return m_output_dir_path.empty() ? m_project_dir_path : m_output_dir_path;
    }

    boost::filesystem::path project_dir(const std::string& app_config_val) const
    {
        boost::system::error_code ec;
        boost::filesystem::path app_config_path{app_config_val};
        if (m_project_dir_path.empty()
            && boost::filesystem::exists(app_config_path, ec)
            && boost::filesystem::is_directory(app_config_path, ec))
        {
            return app_config_path;
        }
        return m_project_dir_path;
    }

    void set_output_dir(const boost::filesystem::path& path) {
        auto p = validate_dir_path(path);
        if (!p.empty()) {
            m_output_dir_path = std::move(p);
        }
    }
    
    void set_project_dir(const boost::filesystem::path& path) {
        auto p = validate_dir_path(path);
        if (!p.empty()) {
            m_project_dir_path = std::move(p);
        }
    }

    void set_output_extension(const std::string& extension)
    {
        m_output_extension = extension;
    }

    std::string output_extension(const std::string& app_config_val) const
    {
        return m_output_extension.empty() ? app_config_val : m_output_extension;
    }
};
}