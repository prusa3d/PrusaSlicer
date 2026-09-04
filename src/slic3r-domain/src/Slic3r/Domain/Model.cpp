#include "Slic3r/Domain/Model.hpp"

namespace Slic3r::Domain {

Model::~Model() { this->clear_objects(); }

Model& Model::operator=(const Model& rhs)
{
    this->assign_copy(rhs);
    assert(this->id().valid());
    assert(this->id() == rhs.id());
    return *this;
}

Model& Model::operator=(Model&& rhs) noexcept
{
    this->assign_copy(std::move(rhs));
    assert(this->id().valid());
    assert(this->id() == rhs.id());
    return *this;
}

void Model::copy_id(const Model& rhs) { ObjectBase::copy_id(rhs); }

Model* Model::new_copy(const Model& rhs)
{
    auto* ret = new Model(rhs);
    assert(ret->id() == rhs.id());
    return ret;
}

Model* Model::new_copy(Model&& rhs)
{
    auto* ret = new Model(std::move(rhs));
    assert(ret->id() == rhs.id());
    return ret;
}

Model Model::make_copy(const Model& rhs)
{
    Model ret(rhs);
    assert(ret.id() == rhs.id());
    return ret;
}

Model Model::make_copy(Model&& rhs)
{
    Model ret(std::move(rhs));
    assert(ret.id() == rhs.id());
    return ret;
}

Model* Model::new_clone(const Model& rhs)
{
    // Default constructor assigning an invalid ID.
    auto obj = new Model(-1);
    obj->assign_clone(rhs);
    assert(obj->id().valid() && obj->id() != rhs.id());
    return obj;
}

Model Model::make_clone(const Model& rhs)
{
    // Default constructor assigning an invalid ID.
    Model obj(-1);
    obj.assign_clone(rhs);
    assert(obj.id().valid() && obj.id() != rhs.id());
    return obj;
}

Model& Model::assign_copy(const Model& rhs)
{
    this->copy_id(rhs);
    // copy objects
    this->clear_objects();
    this->objects.reserve(rhs.objects.size());
    for (const ModelObject* model_object : rhs.objects) {
        // Copy including the ID, leave ID set to invalid (zero).
        auto mo = ModelObject::new_copy(*model_object);
        mo->set_model(this);
        this->objects.emplace_back(mo);
    }

    return *this;
}

Model& Model::assign_copy(Model&& rhs)
{
    this->copy_id(rhs);
    // Move objects, adjust the parent pointer.
    this->clear_objects();
    this->objects = std::move(rhs.objects);
    for (ModelObject* model_object : this->objects) {
        model_object->set_model(this);
    }

    rhs.objects.clear();

    return *this;
}

Model& Model::assign_clone(const Model& rhs)
{
    this->assign_copy(rhs);
    assert(this->id().valid() && this->id() == rhs.id());
    this->assign_new_unique_ids_recursive();
    assert(this->id().valid() && this->id() != rhs.id());
    return *this;
}

void Model::assign_new_unique_ids_recursive()
{
    this->set_new_unique_id();
    for (ModelObject* model_object : this->objects) {
        model_object->assign_new_unique_ids_recursive();
    }
}

void Model::update_links_bottom_up_recursive()
{
    for (ModelObject* model_object : this->objects) {
        model_object->set_model(this);
        for (ModelInstance* model_instance : model_object->instances) {
            model_instance->set_model_object(model_object);
        }

        for (ModelVolume* model_volume : model_object->volumes) {
            model_volume->set_model_object(model_object);
        }
    }
}

void Model::assert_is_valid() const
{
    std::vector<size_t> ids;
    ids.reserve(1 + this->objects.size() * 8);

    const auto collect_id = [&ids](const ObjectID id) {
        ASSERT(id.valid());
        ids.push_back(id.id);
    };

    collect_id(this->id());

    for (const ModelObject* object : this->objects) {
        ASSERT(ASSERT_VAL(object)->get_model() == this);

        collect_id(object->id());
        collect_id(object->layer_height_profile.id());

        for (const ModelInstance* instance : object->instances) {
            ASSERT(instance != nullptr);
            ASSERT(instance->get_object() == object);
            collect_id(instance->id());
        }

        for (const ModelVolume* volume : object->volumes) {
            ASSERT(volume != nullptr);
            ASSERT(volume->get_object() == object);

            collect_id(volume->id());
            collect_id(volume->supported_facets.id());
            collect_id(volume->seam_facets.id());
            collect_id(volume->mm_segmentation_facets.id());
            collect_id(volume->fuzzy_skin_facets.id());
        }
    }

    std::sort(ids.begin(), ids.end());
    ASSERT(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
}

ModelObject* Model::add_object()
{
    this->objects.emplace_back(new ModelObject(this));
    return this->objects.back();
}

ModelObject* Model::add_object(const ModelObject& other)
{
    ModelObject* new_object = ModelObject::new_clone(other);
    new_object->set_model(this);
    this->objects.push_back(new_object);
    return new_object;
}

ModelObject* Model::add_object(std::unique_ptr<ModelObject> model_object)
{
    model_object->set_model(this);
    this->objects.push_back(model_object.release());
    return this->objects.back();
}

void Model::delete_object(size_t idx)
{
    ModelObjectPtrs::iterator i = this->objects.begin() + idx;
    delete *i;
    this->objects.erase(i);
}

bool Model::delete_object(ModelObject* object)
{
    if (object != nullptr) {
        size_t idx = 0;
        for (ModelObject* model_object : this->objects) {
            if (model_object == object) {
                delete model_object;
                this->objects.erase(this->objects.begin() + idx);
                return true;
            }
            ++idx;
        }
    }

    return false;
}

bool Model::delete_object(ObjectID id)
{
    if (id.id != 0) {
        size_t idx = 0;
        for (ModelObject* model_object : this->objects) {
            if (model_object->id() == id) {
                delete model_object;
                this->objects.erase(this->objects.begin() + idx);
                return true;
            }
            ++idx;
        }
    }

    return false;
}

void Model::clear_objects()
{
    for (ModelObject* o : this->objects) {
        delete o;
    }

    this->objects.clear();
}

// makes sure all objects have at least one instance
bool Model::add_default_instances()
{
    // apply a default position to all objects not having one
    for (ModelObject* o : this->objects) {
        if (o->instances.empty()) {
            o->add_instance();
        }
    }

    return true;
}

double Model::max_z() const
{
    double z = 0;
    for (ModelObject* o : this->objects) {
        z = std::max(z, o->max_z());
    }

    return z;
}

bool Model::is_fdm_support_painted() const
{
    return std::any_of(this->objects.cbegin(), this->objects.cend(), [](const ModelObject* mo) { return mo->is_fdm_support_painted(); });
}

bool Model::is_seam_painted() const
{
    return std::any_of(this->objects.cbegin(), this->objects.cend(), [](const ModelObject* mo) { return mo->is_seam_painted(); });
}

bool Model::is_mm_painted() const
{
    return std::any_of(this->objects.cbegin(), this->objects.cend(), [](const ModelObject* mo) { return mo->is_mm_painted(); });
}

bool Model::is_fuzzy_skin_painted() const
{
    return std::any_of(this->objects.cbegin(), this->objects.cend(), [](const ModelObject* mo) { return mo->is_fuzzy_skin_painted(); });
}

size_t Model::minimum_required_painting_version(
    FacetsAnnotation ModelVolume::* facets_annotation_member
) const
{
    size_t version = 1;
    for (const ModelObject* object : this->objects) {
        for (const ModelVolume* volume : object->volumes) {
            version = std::max(
                version,
                (volume->*facets_annotation_member).get_data().minimum_required_painting_version()
            );
        }
    }

    return version;
}

} // namespace Slic3r::Domain
