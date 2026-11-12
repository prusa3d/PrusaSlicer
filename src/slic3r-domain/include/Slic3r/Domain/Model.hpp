#pragma once

#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/Types.hpp"

namespace Slic3r {
class BuildVolume;
} // namespace Slic3r

namespace cereal {
class BinaryInputArchive;
class BinaryOutputArchive;

template<class T>
void load_optional(BinaryInputArchive& ar, std::shared_ptr<const T>& ptr);

template<class T>
void save_optional(BinaryOutputArchive& ar, const std::shared_ptr<const T>& ptr);

template<class T>
void load_by_value(BinaryInputArchive& ar, T& obj);

template<class T>
void save_by_value(BinaryOutputArchive& ar, const T& obj);

} // namespace cereal

namespace Slic3r::Domain {

const constexpr float  SINKING_Z_THRESHOLD     = -0.001f;
const constexpr double SINKING_MIN_Z_THRESHOLD =  0.05;

class ModelWipeTower
{
public:
    Vec2d position{180., 140.};
    double rotation{};

    bool operator==(const ModelWipeTower& other) const
    {
        return position == other.position && rotation == other.rotation;
    }
    bool operator!=(const ModelWipeTower& other) const { return !((*this) == other); }

    // For serialization / deserialization of ModelWipeTower composed into another class into the
    // Undo / Redo stack as a separate object.
};

/**
 * The print bed content.
 * Description of a triangular model with multiple materials, multiple instances with various affine transformations
 * and with multiple modifier meshes.
 * A model groups multiple objects, each object having possibly multiple instances,
 * all objects may share multiple materials.
 */
class Model final : public ObjectBase
{
public:
    // Objects are owned by a model. Each model may have multiple instances, each instance having its own transformation (shift, scale, rotation).
    ModelObjectPtrs objects;

    // Default constructor assigns a new ID to the model.
    Model() { assert(this->id().valid()); }
    ~Model() override;

    /* To be able to return an object from own copy / clone methods. Hopefully the compiler will do the "Copy elision" */
    /* (Omits copy and move(since C++11) constructors, resulting in zero - copy pass - by - value semantics). */
    Model(const Model& rhs) : ObjectBase(-1)
    {
        assert(this->id().invalid());
        this->assign_copy(rhs);
        assert(this->id().valid());
        assert(this->id() == rhs.id());
    }

    Model(Model&& rhs) noexcept : ObjectBase(-1)
    {
        assert(this->id().invalid());
        this->assign_copy(std::move(rhs));
        assert(this->id().valid());
        assert(this->id() == rhs.id());
    }

    Model& operator=(const Model& rhs);
    Model& operator=(Model&& rhs) noexcept;

    void          copy_id(const Model& rhs);

    static Model* new_copy(const Model &rhs);
    static Model* new_copy(Model &&rhs);
    static Model  make_copy(const Model &rhs);
    static Model  make_copy(Model &&rhs);
    static Model* new_clone(const Model &rhs);
    static Model  make_clone(const Model &rhs);

    Model&        assign_copy(const Model& rhs);
    Model&        assign_copy(Model&& rhs);
    Model&        assign_clone(const Model& rhs);

    // Add a new ModelObject to this Model, generate a new ID for this ModelObject.
    ModelObject*  add_object();
    ModelObject*  add_object(const ModelObject& other);
    ModelObject*  add_object(std::unique_ptr<ModelObject> model_object);
    void          delete_object(size_t idx);
    bool          delete_object(ObjectID id);
    bool          delete_object(ModelObject* object);
    void          clear_objects();

    bool          add_default_instances();

    // Return maximum height of all printable objects.
    double        max_z() const;

    // Checks if any of objects is painted using the fdm support painting gizmo.
    bool          is_fdm_support_painted() const;
    // Checks if any of objects is painted using the seam painting gizmo.
    bool          is_seam_painted() const;
    // Checks if any of objects is painted using the multi-material painting gizmo.
    bool          is_mm_painted() const;
    // Checks if any of objects is painted using the fuzzy skin painting gizmo.
    bool          is_fuzzy_skin_painted() const;

    size_t minimum_required_painting_version(
        FacetsAnnotation ModelVolume::* facets_annotation_member
    ) const;

    void update_links_bottom_up_recursive();
    void assert_is_valid() const;

private:
    explicit Model(int) : ObjectBase(-1) { assert(this->id().invalid()); }

    void assign_new_unique_ids_recursive() override;
};
}
