#include "Slic3r/Domain/ObjectID.hpp"
#include <vector>

namespace Slic3r::Domain {

size_t ObjectBase::s_last_id = 0;

struct WipeTowerId : public ObjectBase {
    // Need to inherit because ObjectBase
    // destructor is protected.
    using ObjectBase::ObjectBase;
};

ObjectID wipe_tower_instance_id(size_t bed_idx)
{
    static std::vector<WipeTowerId> mine;
    if (bed_idx >= mine.size()) {
        mine.resize(bed_idx + 1);
    }
    return mine[bed_idx].id();
}

ObjectWithTimestamp::Timestamp ObjectWithTimestamp::s_last_timestamp = 1;

} // namespace Slic3r::Domain

// CEREAL_REGISTER_TYPE(Slic3r::ObjectBase)
