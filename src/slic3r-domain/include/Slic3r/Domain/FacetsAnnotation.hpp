#pragma once

#include "Slic3r/Domain/Forward.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/TriangleSelector.hpp"

#include <cstdint>
#include <string>

namespace Slic3r::Domain {

enum class FacetsAnnotationKind : std::uint8_t
{
    FdmSupports,
    Seam,
    MultiMaterial,
    FuzzySkin
};

class FacetsAnnotation final : public Domain::ObjectWithTimestamp
{
public:
    Domain::TriangleSelector::TriangleSplittingData triangle_splitting_data;

    // Assign the content if the timestamp differs, don't assign an ObjectID.
    void assign(const FacetsAnnotation &rhs);
    void assign(FacetsAnnotation &&rhs);

    const Domain::TriangleSelector::TriangleSplittingData &get_data() const noexcept;

    bool set_data(TriangleSelector::TriangleSplittingData&& new_triangle_splitting_data);

    bool empty() const;

    // Following method clears the config and increases its timestamp, so the deleted
    // state is considered changed from perspective of the undo/redo stack.
    void reset();

    // Before deserialization, reserve space for n_triangles.
    void reserve(int n_triangles);

    // Serialize triangle into string, for serialization into 3MF/AMF.
    std::string get_triangle_as_string(int triangle_idx) const;

    // Deserialize triangles one by one, with strictly increasing triangle_id.
    void set_triangle_from_string(int triangle_id, const std::string& str);

    // After deserializing the last triangle, shrink data to fit.
    void shrink_to_fit();

private:
    // Constructors to be only called by derived classes.
    // Default constructor to assign a unique ID.
    explicit FacetsAnnotation() = default;
    // Constructor with ignored int parameter to assign an invalid ID, to be replaced
    // by an existing ID copied from elsewhere.
    explicit FacetsAnnotation(int) : ObjectWithTimestamp(-1) {}
    // Copy constructor copies the ID.
    FacetsAnnotation(const FacetsAnnotation& rhs) = default;
    // Move constructor copies the ID.
    FacetsAnnotation(FacetsAnnotation&& rhs) = default;

    // called by ModelVolume::assign_copy()
    FacetsAnnotation& operator=(const FacetsAnnotation& rhs) = default;
    FacetsAnnotation& operator=(FacetsAnnotation&& rhs) = default;

    // To access set_new_unique_id() when copy / pasting a ModelVolume.
    friend class ModelVolume;
};

} // namespace Slic3r::Domain
