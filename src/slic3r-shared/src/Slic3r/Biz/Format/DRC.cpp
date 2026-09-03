#include "Slic3r/Biz/Format/DRC.hpp"

#include <string>
#include <utility>
#include <cstring>

#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/nowide/cstdio.hpp>

#include <draco/compression/encode.h>
#include <draco/compression/decode.h>
#include <draco/io/mesh_io.h>
#include <draco/mesh/mesh.h>

#include "fmt/format.h"

#include "Slic3r/Biz/Algorithms/Model.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"

using namespace draco;

namespace Slic3r::Biz {

tl::expected<Domain::TriangleMesh, std::string> load_drc(const std::string& path)
{
    try {
        boost::iostreams::mapped_file_source file(path);

        DecoderBuffer buffer;
        buffer.Init(file.data(), file.size());

        auto geotype = Decoder::GetEncodedGeometryType(&buffer);
        if (!geotype.ok())
            return tl::make_unexpected(fmt::format("load_drc: error getting geometry type for {}.", path));
        if (geotype.value() != TRIANGULAR_MESH)
            return tl::make_unexpected(fmt::format("load_drc: geometry is not triangular mesh for {}.", path));

        Decoder decoder;
        Mesh dracoMesh;
        Status status = decoder.DecodeBufferToGeometry(&buffer, &dracoMesh);
        if (!status.ok())
            return tl::make_unexpected(fmt::format("load_drc: error decoding geometry for {}.", path));


        const PointAttribute *const positions = dracoMesh.GetNamedAttribute(GeometryAttribute::POSITION);
        if (positions == NULL)
            return tl::make_unexpected(fmt::format("load_drc: error decoding vertices for {}.", path));
        if (positions->num_components() != 3)
            return tl::make_unexpected(fmt::format("load_drc: trianglar mesh did not contain triangles for {}.", path));
        
        size_t num_vertices = positions->size();
        
        indexed_triangle_set its;
        its.vertices.reserve(num_vertices);
        for (AttributeValueIndex i(0); i < num_vertices; ++ i) {
            float pos[3];
            positions->ConvertValue<float>(i, 3, pos);
            its.vertices.emplace_back(pos[0], pos[1], pos[2]);
        }

        size_t num_faces = dracoMesh.num_faces();
        its.indices.reserve(num_faces);
        for (FaceIndex i(0); i < num_faces; ++ i) {
            Mesh::Face face = dracoMesh.face(i);

            its.indices.push_back(Domain::Index3{
                static_cast<int>(positions->mapped_index(face[0]).value()),
                static_cast<int>(positions->mapped_index(face[1]).value()),
                static_cast<int>(positions->mapped_index(face[2]).value())
            });
        }

        using Biz::Algorithms::TriangleMesh::construct;
        Domain::TriangleMesh mesh_out(construct(std::move(its)));
        if (mesh_out.empty())
            return tl::make_unexpected(fmt::format("load_obj: This Draco file couldn't be read because it's empty. {}", path));
        if (mesh_out.volume() < 0)
            mesh_out.flip_triangles();
        return mesh_out;
    } catch (const std::exception& e) {
        return tl::make_unexpected(fmt::format("load_drc: exception while loading {}: {}", path, e.what()));
    }
}

}; // namespace Slic3r::Biz
