#pragma once

#include <cstdint>
#include <vector>
#include <cstdlib>

#include "Types.hpp"

namespace Slic3r::App::Render {

/**
 * Description of a single vertex attribute stored in VertexBuffer.
 */
struct VertexAttribDesc
{
    /**
     * Attribute semantic
     */
    VertexAttribType attrib_type;

    /**
     * Attribute data type
     */
    DataType data_type;

    /**
     * Number of components
     */
    uint8_t components;

    /**
     * Offset from start in bytes
     */
    size_t offset;

    /**
     * Enable normalization during upload.
     */
    bool normalize{false};

    /**
     * Treat attribute data type as float.
     */
    bool cast_to_float{ false };

    /**
     * All components size in bytes for given vertex attribute.
     * @return Vertex attribute size in bytes.
     */
    size_t size_in_bytes() const;
};

using VertexAttribsDesc = std::vector<VertexAttribDesc>;
size_t vertex_attribs_stride(const VertexAttribsDesc& attrs);
size_t index_type_size(IndexType index);

}
