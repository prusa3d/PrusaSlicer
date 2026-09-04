#pragma once

#include <vector>
#include <tuple>
#include <boost/functional/hash.hpp>
#include <cstdint>
#include "Slic3r/Domain/SlicingId.hpp"

namespace Slic3r::Domain {

struct ElementRef
{
    size_t object_id{0};
    size_t instance_id{0};
    size_t volume_id{0};
    SlicingId wipe_tower_id{};

    ElementRef() = default;
    ElementRef(const ElementRef&) = default;
    ElementRef(ElementRef&&) = default;

    ElementRef(size_t obj_id, size_t inst_id = 0, size_t vol_id = 0) :
        object_id(obj_id),
        instance_id(inst_id),
        volume_id(vol_id)
    {}

    explicit ElementRef(SlicingId wipe_tower_id) : wipe_tower_id(wipe_tower_id) {}

    ElementRef& operator=(const ElementRef&) = default;
    ElementRef& operator=(ElementRef&&) = default;

    bool operator==(const ElementRef& rhs) const { return as_tuple() == rhs.as_tuple(); }

    bool operator!=(const ElementRef& rhs) const { return as_tuple() != rhs.as_tuple(); }

    bool operator<(const ElementRef& rhs) const { return as_tuple() < rhs.as_tuple(); }

    bool operator>(const ElementRef& rhs) const { return as_tuple() > rhs.as_tuple(); }

    bool operator<=(const ElementRef& rhs) const { return as_tuple() <= rhs.as_tuple(); }

    bool operator>=(const ElementRef& rhs) const { return as_tuple() >= rhs.as_tuple(); }

    bool has_instance() const
    {
        return instance_id != 0;
    }

    bool has_volume() const
    {
        return volume_id != 0;
    }

    bool is_wipe_tower() const
    {
        return wipe_tower_id != SlicingId{};
    }

    bool is_part_of(const ElementRef& this_or_parent) const
    {
        if (is_wipe_tower()) {
            return wipe_tower_id == this_or_parent.wipe_tower_id;
        }
        return object_id == this_or_parent.object_id && this_or_parent.instance_id == instance_id &&
            (volume_id == this_or_parent.volume_id || this_or_parent.volume_id == 0);
    }

private:
    constexpr std::tuple<size_t, size_t, size_t, size_t, size_t> as_tuple() const
    {
        return std::tie(
            object_id,
            instance_id,
            volume_id,
            wipe_tower_id.project_id,
            wipe_tower_id.bed_instance_id
        );
    }
};

using ElementRefs = std::vector<ElementRef>;

} // namespace Slic3r::Domain


namespace std {

template<>
struct hash<Slic3r::Domain::ElementRef>
{
    using value_type = Slic3r::Domain::ElementRef;

    std::uint64_t operator()(const value_type& val) const
    {
        size_t ret = boost::hash_value(val.object_id);
        boost::hash_combine(ret, val.instance_id);
        boost::hash_combine(ret, val.volume_id);
        return ret;
    }
};

} // namespace std
