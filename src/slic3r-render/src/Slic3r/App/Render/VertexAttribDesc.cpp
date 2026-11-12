#include "Slic3r/App/Render/VertexAttribDesc.hpp"

#include "Slic3r/Assert.hpp"

namespace Slic3r::App::Render {

size_t size_in_bytes(DataType data_type)
{
    switch (data_type) {
    case DataType::Byte:
    case DataType::UByte:
        return sizeof(uint8_t);
    case DataType::Float:
        return sizeof(float);
    case DataType::Short:
        return sizeof(uint16_t);
    default:
        // unknown type, please add case for it
        ASSERT(false);
        return 0;
    }
}

size_t VertexAttribDesc::size_in_bytes() const { return Render::size_in_bytes(data_type) * components; }
size_t vertex_attribs_stride(const VertexAttribsDesc& attrs)
{
    size_t ret = 0;
    for (const auto& attr: attrs)
        ret += attr.size_in_bytes();
    return ret;
}

size_t index_type_size(IndexType index)
{
    switch (index) {
    case IndexType::UByte:
        return 1;
    case IndexType::UShort:
        return 2;
    case IndexType::UInt:
        return 4;
    }

    // unsupported index type
    ASSERT(false);
    return 4;
}

}
