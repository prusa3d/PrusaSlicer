#include "Slic3r/Biz/Format/STL.hpp"

#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"

#include "admesh/stl.h"

#include <optional>
#include "boost/nowide/cstdio.hpp"
#include <Slic3r/Log.hpp>
#include <boost/predef/other/endian.h>

using Slic3r::Domain::TriangleMesh;

namespace Slic3r::Biz {

using TriangleMesh = Domain::TriangleMesh;

#if BOOST_ENDIAN_LITTLE_BYTE
static inline void big_endian_reverse_quads(char*, size_t) {}
#else // BOOST_ENDIAN_LITTLE_BYTE
static inline void big_endian_reverse_quads(char* buf, size_t cnt)
{
    for (size_t i = 0; i < cnt; i += 4) {
        std::swap(buf[i], buf[i + 3]);
        std::swap(buf[i + 1], buf[i + 2]);
    }
}
#endif // BOOST_ENDIAN_LITTLE_BYTE

static bool its_write_stl_ascii(
    const std::string& file,
    const std::string& label,
    const std::vector<stl_triangle_vertex_indices>& indices,
    const std::vector<stl_vertex>& vertices
)
{
    FILE* fp = boost::nowide::fopen(file.c_str(), "w");
    if (fp == nullptr) {
        SPDLOG_ERROR("its_write_stl_ascii: Couldn't open {} for writing", file);
        return false;
    }

    fprintf(fp, "solid  %s\n", label.c_str());

    for (const stl_triangle_vertex_indices& face : indices) {
        Domain::Vec3f vertex[3] = {vertices[face[0]], vertices[face[1]], vertices[face[2]]};
        Domain::Vec3f normal    = (vertex[1] - vertex[0]).cross(vertex[2] - vertex[1]).normalized();
        fprintf(fp, "  facet normal % .8E % .8E % .8E\n", normal(0), normal(1), normal(2));
        fprintf(fp, "    outer loop\n");
        fprintf(fp, "      vertex % .8E % .8E % .8E\n", vertex[0](0), vertex[0](1), vertex[0](2));
        fprintf(fp, "      vertex % .8E % .8E % .8E\n", vertex[1](0), vertex[1](1), vertex[1](2));
        fprintf(fp, "      vertex % .8E % .8E % .8E\n", vertex[2](0), vertex[2](1), vertex[2](2));
        fprintf(fp, "    endloop\n");
        fprintf(fp, "  endfacet\n");
    }

    fprintf(fp, "endsolid  %s\n", label.c_str());
    fclose(fp);
    return true;
}

static bool its_write_stl_binary(
    const std::string& file,
    const std::string& label,
    const std::vector<stl_triangle_vertex_indices>& indices,
    const std::vector<stl_vertex>& vertices
)
{
    FILE* fp = boost::nowide::fopen(file.c_str(), "wb");
    if (fp == nullptr) {
        SPDLOG_ERROR("its_write_stl_binary: Couldn't open {} for writing", file);
        return false;
    }

    {
        static constexpr const size_t header_size = 80;
        std::vector<char> header(header_size, 0);
        if (int header_len = std::min(label.size(), header_size); header_len > 0)
            ::memcpy(header.data(), label.c_str(), header_len);
        ::fwrite(header.data(), header_size, 1, fp);
    }

    uint32_t nfaces = indices.size();
    big_endian_reverse_quads(reinterpret_cast<char*>(&nfaces), 4);
    ::fwrite(&nfaces, 4, 1, fp);

    stl_facet f;
    f.extra[0] = 0;
    f.extra[1] = 0;
    for (const Domain::Index3& face : indices) {
        f.vertex[0] = vertices[face[0]];
        f.vertex[1] = vertices[face[1]];
        f.vertex[2] = vertices[face[2]];
        f.normal    = (f.vertex[1] - f.vertex[0]).cross(f.vertex[2] - f.vertex[1]).normalized();
        big_endian_reverse_quads(reinterpret_cast<char*>(&f), 48);
        fwrite(&f, 50, 1, fp);
    }

    fclose(fp);
    return true;
}

static tl::expected<TriangleMesh, std::string> read_stl_file(const char* input_file, bool repair)
{
    stl_file stl;
    if (!stl_open(&stl, input_file))
        return tl::make_unexpected("Unable to load STL file.");
    if (repair)
        Algorithms::TriangleMesh::trianglemesh_repair_on_import(stl);

    Domain::TriangleMeshStats stats;
    stats.min              = stl.stats.min;
    stats.max              = stl.stats.max;
    stats.size             = stl.stats.size;
    stats.volume           = stl.stats.volume;

    auto facets_w_1_bad_edge = stl.stats.connected_facets_2_edge - stl.stats.connected_facets_3_edge;
    auto facets_w_2_bad_edge = stl.stats.connected_facets_1_edge - stl.stats.connected_facets_2_edge;
    auto facets_w_3_bad_edge = stl.stats.number_of_facets - stl.stats.connected_facets_1_edge;
    stats.open_edges         = stl.stats.backwards_edges
        + facets_w_1_bad_edge
        + facets_w_2_bad_edge * 2
        + facets_w_3_bad_edge * 3;

    stats.repaired_errors = {
        stl.stats.edges_fixed,
        stl.stats.degenerate_facets,
        stl.stats.facets_removed,
        stl.stats.facets_reversed,
        stl.stats.backwards_edges
    };

    stats.number_of_parts = stl.stats.number_of_parts;

    indexed_triangle_set its;
    stl_generate_shared_vertices(&stl, its);
    return TriangleMesh{std::move(its), std::move(stats)};
}

tl::expected<Domain::TriangleMesh, std::string> load_stl(const std::string& path)
{
    auto mesh = read_stl_file(path.c_str(), true);
    if (!mesh) {
        return mesh;
    }
    if (mesh.value().empty()) {
        return tl::make_unexpected("This STL file couldn't be read because it's empty.");
    }
    return mesh;
}

bool
store_stl(const std::string& path, const TriangleMesh& mesh, bool binary, const std::string& label)
{
    if (binary) {
        return its_write_stl_binary(path, label, mesh.its.indices, mesh.its.vertices);
    }

    return its_write_stl_ascii(path, label, mesh.its.indices, mesh.its.vertices);
}

} // namespace Slic3r::Biz
