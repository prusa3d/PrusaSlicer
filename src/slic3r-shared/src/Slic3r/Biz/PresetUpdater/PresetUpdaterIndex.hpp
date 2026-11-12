#pragma once

#include "Slic3r/Semver.hpp"

#include <boost/filesystem/path.hpp>

namespace Slic3r::Biz::PresetUpdater {

struct Version
{
    // Version of this config.
    Slic3r::Semver config_version = Slic3r::Semver::invalid();
    // Minimum Slic3r version, for which this config is applicable.
    Slic3r::Semver min_slic3r_version = Slic3r::Semver::zero();
    // Maximum Slic3r version, for which this config is recommended.
    // Slic3r should read older configuration and upgrade to a newer format,
    // but likely there has been a better configuration published, using the new features.
    Slic3r::Semver max_slic3r_version = Slic3r::Semver::inf();
    // Single comment line.
    std::string comment;

    bool is_slic3r_supported(const Slic3r::Semver& slicer_version) const;
    bool is_current_slic3r_supported() const;
    bool is_current_slic3r_downgrade() const;
};

// Index of vendor specific config bundle versions and Slic3r compatibilities.
// The index is being downloaded from the internet, also an initial version of the index
// is contained in the Slic3r installation.
//
// The index has a simple format:
//
// min_sic3r_version =
// max_slic3r_version =
// config_version "comment"
// config_version "comment"
// ...
// min_slic3r_version =
// max_slic3r_version =
// config_version comment
// config_version comment
// ...
//
// The min_slic3r_version, max_slic3r_version keys are applied to the config versions below,
// empty slic3r version means an open interval.
class PresetUpdaterIndex
{
public:
    typedef std::vector<Version>::const_iterator const_iterator;
    // Read a config index file in the simple format described in the Index class comment.
    // Throws Slic3r::file_parser_error and the standard std file access exceptions.
    size_t load(const boost::filesystem::path& path);

    const std::string& vendor() const
    {
        return m_vendor;
    }

    // Returns version of the index as the highest version of all the configs.
    // If there is no config, Semver::zero() is returned.
    Slic3r::Semver version() const;

    const_iterator begin() const
    {
        return m_configs.begin();
    }

    const_iterator end() const
    {
        return m_configs.end();
    }

    const_iterator find(const Slic3r::Semver& ver) const;

    const std::vector<Version>& configs() const
    {
        return m_configs;
    }

    // Finds a recommended config to be installed for the current Slic3r version.
    // Returns configs().end() if such version does not exist in the index. This shall never happen
    // if the index is valid.
    const_iterator recommended() const;
    // Recommended config for a provided slic3r version. Used when checking for slic3r update (slic3r_version is the old one read out from PrusaSlicer.ini)
    const_iterator recommended(const Slic3r::Semver& slic3r_version) const;

    // Returns the filesystem path from which this index has originally been loaded
    const boost::filesystem::path& path() const
    {
        return m_path;
    }

    // Load all vendor specific indices.
    // Throws Slic3r::file_parser_error and the standard std file access exceptions.
    /*
    static std::vector<Index>   load_db();
    */

private:
    std::string m_vendor;
    std::vector<Version> m_configs;
    boost::filesystem::path m_path;
};

bool is_idx_file(const boost::filesystem::directory_entry& path);

} // namespace Slic3r::Biz::PresetUpdater
