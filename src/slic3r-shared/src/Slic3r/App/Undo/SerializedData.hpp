#pragma once

#include <map>
#include <memory>
#include <string>
#include <variant>

#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/App/Undo/ChannelId.hpp"

namespace Slic3r::App::Undo {

struct TriangleMeshChunk {
    std::shared_ptr<const Domain::TriangleMesh> mesh;

    bool operator==(const TriangleMeshChunk& other) const
    {
        return mesh.get() == other.mesh.get();
    }
};

struct VersionedChunk
{
    std::string serialized_data;
    std::size_t version;

    bool operator==(const VersionedChunk& other) const
    {
        return version == other.version;
    }
};

struct ConfigContainerChunk
{
    std::string serialized_data;
    std::tuple<std::string, std::size_t> config_id{};

    bool operator==(const ConfigContainerChunk& other) const = default;
};

using Chunk = std::variant<std::string, TriangleMeshChunk, VersionedChunk, ConfigContainerChunk>;

struct SerializedData
{
    std::map<ChannelId, Chunk> separate_chunks;
    std::string serialized_data;
};

} // namespace Slic3r::Biz::Undo
