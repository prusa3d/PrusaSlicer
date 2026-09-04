#pragma once

#include "Slic3r/App/Init.hpp"

#include <cstddef>
#include <string>

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
};

const std::vector<TestProfileSet>& resolved_profile_sets();

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

} // namespace Slic3r::App::CLI::Test
