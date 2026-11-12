#pragma once
#include <vector>

namespace Slic3r::Domain {

struct BedRef
{
    std::size_t config_container_id{0};
    std::size_t instance_id{0};
};

inline bool operator == (const BedRef& br1, const BedRef& br2)
{
    return
        br1.config_container_id == br2.config_container_id &&
        br1.instance_id == br2.instance_id;
}

inline bool operator < (const BedRef& lhs, const BedRef& rhs)
{
    return lhs.config_container_id < rhs.config_container_id ||
        (lhs.config_container_id == rhs.config_container_id && lhs.instance_id < rhs.instance_id);
}

using BedRefs = std::vector<BedRef>;

}
