#pragma once

#include "Slic3r/App/Init.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>

namespace Slic3r::App::CLI::Test {

/**
 * @brief A consistent set of profile names picked from the real preset bundle.
 */
struct TestProfileSet
{
    std::string printer_profile_name;
    std::string print_profile_name;
    std::string material_profile_name;
    std::size_t tool_count{0};
    // Tool overrides are valid in multi-tool prints only.
    bool multi_extruder{false};
};

/**
 * @brief FFF profile sets, one per FFF printer in the preset bundle.
 */
const std::vector<TestProfileSet>& resolved_profile_sets();

/**
 * @brief SLA profile sets, one per SLA printer in the preset bundle.
 */
const std::vector<TestProfileSet>& resolved_sla_profile_sets();

/**
 * @brief Unique temporary directory removed at the end of the scope.
 */
class ScopedTempDir final
{
public:
    ScopedTempDir();
    ~ScopedTempDir();

    ScopedTempDir(const ScopedTempDir&)            = delete;
    ScopedTempDir& operator=(const ScopedTempDir&) = delete;
    ScopedTempDir(ScopedTempDir&&)                 = delete;
    ScopedTempDir& operator=(ScopedTempDir&&)      = delete;

    [[nodiscard]] const boost::filesystem::path& path() const
    {
        return m_path;
    }

private:
    boost::filesystem::path m_path;
};

/**
 * @brief Writes a cube_size_mm cube into a binary STL file and returns its path.
 */
boost::filesystem::path write_cube_stl(
    const boost::filesystem::path& directory,
    const std::string& file_name,
    double cube_size_mm
);

InitParams make_params(const TestProfileSet& profile_set);

/**
 * @brief Counts the regular files with a .gcode or .bgcode extension in the directory.
 */
std::size_t count_gcode_files(const boost::filesystem::path& directory);

/**
 * @brief Lists the regular files with a .sl1 or .sl1s extension in the directory.
 */
std::vector<boost::filesystem::path> find_sla_archives(const boost::filesystem::path& directory);

/**
 * @brief Returns the content of an entry of a ZIP archive, or nullopt if it cannot be read.
 */
std::optional<std::string> read_zip_entry(
    const boost::filesystem::path& archive_path,
    const std::string& entry_name
);

/**
 * @brief Returns the content of the file.
 */
std::string read_file(const boost::filesystem::path& path);

} // namespace Slic3r::App::CLI::Test
