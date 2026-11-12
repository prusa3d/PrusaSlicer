#pragma once

#include "Slic3r/Domain/ConfigBoxesFDM.hpp"
#include "Slic3r/Domain/Forward.hpp"
#include "Slic3r/Domain/ObjectID.hpp"

#include <map>

namespace Slic3r::Domain {
class LayerHeightProfile;
} // namespace Slic3r::Domain

namespace cereal {
template <class Archive>
void serialize(Archive& ar, Slic3r::Domain::LayerHeightProfile& profile);
} // namespace cereal

namespace Slic3r::Domain {

struct ZHeightPair
{
    double z{0.};
    double layer_height{0.};

    bool operator==(const ZHeightPair&) const = default;
};

using ZHeightPairs = std::vector<ZHeightPair>;

struct LayerZRange
{
    double bottom_z{0.};
    double top_z{0.};

    double height() const;
    double middle_z() const;
};

using LayerZRanges = std::vector<LayerZRange>;

class LayerHeightProfile final : public Domain::ObjectWithTimestamp
{
private:
    ZHeightPairs m_data;

public:
    const ZHeightPairs& get() const noexcept;
    bool empty() const noexcept;
    void set(const ZHeightPairs& data);
    void set(ZHeightPairs&& data);
    void clear();

    // Assign the content if the timestamp differs, don't assign an ObjectID.
    void assign(const LayerHeightProfile& rhs);
    void assign(LayerHeightProfile&& rhs);

private:
    // Constructors to be only called by derived classes.
    // Default constructor to assign a unique ID.
    explicit LayerHeightProfile() = default;

    // Constructor with ignored int parameter to assign an invalid ID, to be replaced
    // by an existing ID copied from elsewhere.
    explicit LayerHeightProfile(int) : ObjectWithTimestamp(-1) {}

    // Copy constructor copies the ID.
    LayerHeightProfile(const LayerHeightProfile& rhs) = default;
    // Move constructor copies the ID.
    LayerHeightProfile(LayerHeightProfile&& rhs) = default;

    // called by ModelObject::assign_copy()
    LayerHeightProfile& operator=(const LayerHeightProfile& rhs) = default;
    LayerHeightProfile& operator=(LayerHeightProfile&& rhs)      = default;

    // To access set_new_unique_id() when copy / pasting an object.
    friend class ModelObject;

    template <class Archive>
    friend void cereal::serialize(Archive& ar, LayerHeightProfile& profile);
};

using LayerHeightRange  = std::pair<double, double>;
using LayerConfigRanges = std::map<LayerHeightRange, VolumeSettings>;

} // namespace Slic3r::Domain
