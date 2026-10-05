#pragma once

#include "Slic3r/Biz/Algorithms/ZipArchive.hpp"
#include "Slic3r/Biz/Format/ProjectFileConstants.hpp"

#include <catch2/catch_test_macros.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::Biz::FileOpen::Tests {

using ZipEntries = std::vector<std::pair<std::string, std::string>>;

inline boost::filesystem::path write_zip(const boost::filesystem::path& file, const ZipEntries& entries)
{
    {
        Algorithms::ZipArchive writer =
            Algorithms::ZipArchive::open(file.string(), Algorithms::ZipArchive::Mode::Write);
        REQUIRE(writer.is_open());
        for (const auto& [name, data] : entries) {
            REQUIRE(writer.add_entry(name, data.data(), data.size()));
        }
    }
    return file;
}

inline std::string read_file(const boost::filesystem::path& file)
{
    boost::nowide::ifstream stream(file.string(), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

inline void write_file(const boost::filesystem::path& file, const std::string& data)
{
    boost::nowide::ofstream stream(file.string(), std::ios::binary);
    stream.write(data.data(), data.size());
}

/**
 * @brief ASCII STL of a 10 mm tetrahedron, loads as a real object.
 */
inline std::string make_stl_bytes()
{
    return "solid tetra\n"
           "facet normal 0 0 -1\n"
           " outer loop\n  vertex 0 0 0\n  vertex 0 10 0\n  vertex 10 0 0\n endloop\n"
           "endfacet\n"
           "facet normal 0 -1 0\n"
           " outer loop\n  vertex 0 0 0\n  vertex 10 0 0\n  vertex 0 0 10\n endloop\n"
           "endfacet\n"
           "facet normal -1 0 0\n"
           " outer loop\n  vertex 0 0 0\n  vertex 0 0 10\n  vertex 0 10 0\n endloop\n"
           "endfacet\n"
           "facet normal 0.577 0.577 0.577\n"
           " outer loop\n  vertex 10 0 0\n  vertex 0 10 0\n  vertex 0 0 10\n endloop\n"
           "endfacet\n"
           "endsolid tetra\n";
}

inline std::string make_3mf_bytes(const boost::filesystem::path& scratch_dir, bool with_project)
{
    ZipEntries entries{{"3D/3dmodel.model", "<model/>"}};
    if (with_project) {
        entries.emplace_back(std::string(Format::ProjectFileConstants::PRUSA_PROJECT_FILEPATH), "{}");
    }
    const boost::filesystem::path file = scratch_dir / boost::filesystem::unique_path("%%%%%%.3mf");
    write_zip(file, entries);
    std::string bytes = read_file(file);
    boost::filesystem::remove(file);
    return bytes;
}

} // namespace Slic3r::Biz::FileOpen::Tests
