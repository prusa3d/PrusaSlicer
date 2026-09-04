#pragma once

#include <compare>
#include <cstddef>
#include "Slic3r/Assert.hpp"

namespace Slic3r::App::Undo {

struct Id
{
    Id() = default;
    Id(std::size_t value) : value{value}
    {
        ASSERT(value > 0);
    }

    std::size_t value;

    std::strong_ordering operator<=>(const Id&) const = default;
};

struct TriangleMeshId : public Id
{
    using Id::Id;
};

struct ObjectId : public Id
{
    using Id::Id;
};

struct ConfigContainerId : public Id
{
    using Id::Id;
};

struct BedInstancesId : public Id
{
    using Id::Id;
};

struct VirtualExtrudersId : public Id
{
    using Id::Id;
};

using ChannelId =
    std::variant<TriangleMeshId, ObjectId, ConfigContainerId, BedInstancesId, VirtualExtrudersId>;

} // namespace Slic3r::App::Undo
