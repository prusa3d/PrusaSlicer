#include "Slic3r/Biz/Format/OBJ.hpp"

#include <string>
#include <utility>
#include <cassert>
#include <cstring>

#include "fmt/format.h"

#include "Slic3r/Biz/Algorithms/Model.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "objparser.hpp"

namespace Slic3r::Biz {


tl::expected<Domain::TriangleMesh, std::string> load_obj(const std::string& path)
{    
    // Parse the OBJ file.
    ObjParser::ObjData data;
    if (! ObjParser::objparse(path.c_str(), data)) {
        return tl::make_unexpected(fmt::format("load_obj: failed to parse {}.", path));
    }
    
    // Count the faces and verify, that all faces are triangular.
    size_t num_faces = 0;
    size_t num_quads = 0;
    for (size_t i = 0; i < data.vertices.size(); ++ i) {
        // Find the end of face.
        size_t j = i;
        for (; j < data.vertices.size() && data.vertices[j].coordIdx != -1; ++ j) ;
        if (size_t num_face_vertices = j - i; num_face_vertices > 0) {
            if (num_face_vertices > 4) {
                // Non-triangular and non-quad faces are not supported as of now.
                return tl::make_unexpected(fmt::format("load_obj: failed to parse {}. The file contains polygons with more than 4 vertices.", path));
            } else if (num_face_vertices < 3) {
                // Non-triangular and non-quad faces are not supported as of now.
                return tl::make_unexpected(fmt::format("load_obj: failed to parse {}. The file contains polygons with less than 2 vertices.", path));
            }
            if (num_face_vertices == 4)
                ++ num_quads;
            ++ num_faces;
            i = j;
        }
    }
    
    // Convert ObjData into indexed triangle set.
    indexed_triangle_set its;
    size_t num_vertices = data.coordinates.size() / 4;
    its.vertices.reserve(num_vertices);
    its.indices.reserve(num_faces + num_quads);
    for (size_t i = 0; i < num_vertices; ++ i) {
        size_t j = i << 2;
        its.vertices.emplace_back(data.coordinates[j], data.coordinates[j + 1], data.coordinates[j + 2]);
    }
    int indices[4];
    for (size_t i = 0; i < data.vertices.size();)
        if (data.vertices[i].coordIdx == -1)
            ++ i;
        else {
            int cnt = 0;
            while (i < data.vertices.size())
                if (const ObjParser::ObjVertex &vertex = data.vertices[i ++]; vertex.coordIdx == -1) {
                    break;
                } else {
                    assert(cnt < 4);
                    if (vertex.coordIdx < 0 || vertex.coordIdx >= int(its.vertices.size())) {
                        return tl::make_unexpected(fmt::format("load_obj: failed to parse {}. The file contains invalid vertex index.", path));
                    }
                    indices[cnt ++] = vertex.coordIdx;
                }
            if (cnt) {
                assert(cnt == 3 || cnt == 4);
                // Insert one or two faces (triangulate a quad).
                its.indices.push_back(Domain::Index3{indices[0], indices[1], indices[2]});
                if (cnt == 4)
                    its.indices.push_back(Domain::Index3{indices[0], indices[2], indices[3]});
            }
        }

    using Biz::Algorithms::TriangleMesh::construct;
    Domain::TriangleMesh mesh_out(construct(std::move(its)));
    if (mesh_out.empty()) {
        return tl::make_unexpected(fmt::format("load_obj: This OBJ file couldn't be read because it's empty. {}", path));
    }
    if (mesh_out.volume() < 0)
        mesh_out.flip_triangles();
    return mesh_out;
}

bool store_obj(const std::string& path, const Domain::TriangleMesh& mesh)
{
    return Algorithms::TriangleMesh::write_obj_file(mesh, path.c_str());
}

bool store_obj(const std::string& path, Domain::Model* model)
{
    Domain::TriangleMesh mesh = Algorithms::Model::flatten_to_mesh(*model);
    return store_obj(path, mesh);
}

}; // namespace Slic3r::Biz
