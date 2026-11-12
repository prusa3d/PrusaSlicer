#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Uuid.hpp"

#include <ranges>

namespace Slic3r::Domain {

Project::Project() : m_metadata(generate_uuid()), m_model(new Model()) {}

Project::Project(const Project& project) :
    m_metadata(project.metadata()),
    m_file_name(project.file_name()),
    m_loaded_file_path(project.loaded_file_path()),
    m_directory_storage(project.m_directory_storage)
{
    // Preserve IDs
    m_model.reset(Model::new_copy(project.model()));

    m_bed_container = project.bed_container().copy();

    std::unordered_map<const Bed*, const Bed*> bed_translation_table;
    for (const BedPtr& bed : project.bed_container().beds()) {
        const Bed* new_bed = find_bed_by_id(bed->id().id);
        ASSERT(new_bed, "copied bed needs to be found");
        bed_translation_table[bed.get()] = new_bed;
    }

    std::unordered_map<ModelObject*, ModelObject*> model_object_translation_table;
    std::unordered_map<ModelInstance*, ModelInstance*> model_instance_translation_table;
    for (ModelObject* object : project.model().objects) {
        ModelObject* new_object = find_object_by_id(object->id().id);
        ASSERT(new_object, "copied object needs to be found");
        model_object_translation_table[object] = new_object;

        for (ModelInstance* instance : object->instances) {
            ModelInstance* new_instance = find_instance_by_id(instance->id().id);
            ASSERT(new_object, "copied instance needs to be found");
            model_instance_translation_table[instance] = new_instance;
        }
    }

    m_unplaced_model_instances.reserve(project.unplaced_model_instances().size());
    std::ranges::transform(
        project.unplaced_model_instances(),
        std::back_inserter(m_unplaced_model_instances),
        [&](ModelInstance* instance) -> ModelInstance*
        {
            auto it = model_instance_translation_table.find(instance);
            ASSERT(it != model_instance_translation_table.end());
            return it->second;
        }
    );

    m_config_containers.reserve(project.config_containers().size());
    std::ranges::transform(
        project.config_containers(),
        std::back_inserter(m_config_containers),
        [&](const std::unique_ptr<ConfigContainer>& config_container)
        { return config_container->copy(bed_translation_table, model_instance_translation_table); }
    );
}

const ConfigContainer* Project::find_config_container(size_t id) const
{
    return find_by_id<ConfigContainer>(m_config_containers, id);
}

ConfigContainer* Project::find_config_container(size_t id)
{
    return find_by_id<ConfigContainer>(m_config_containers, id);
}

const ConfigContainer* Project::find_config_container_by_bed_instance_id(size_t id) const
{
    for (const auto& cc : m_config_containers) {
        if (find_by_id(cc->bed_instances(), id))
            return cc.get();
    }
    return nullptr;
}

ConfigContainer* Project::find_config_container_by_bed_instance_id(size_t id)
{
    for (auto& cc : m_config_containers) {
        if (find_by_id(cc->bed_instances(), id))
            return cc.get();
    }
    return nullptr;
}

const Domain::ModelObject* Project::find_object_by_id(size_t id) const
{
    return find_by_id<Domain::ModelObject>(m_model->objects, id);
}

const Domain::ModelVolume* Project::find_volume_by_id(size_t obj_id, size_t vol_id) const
{
    const auto* obj = find_object_by_id(obj_id);
    if (obj == nullptr)
        return nullptr;
    return find_by_id<Domain::ModelVolume>(obj->volumes, vol_id);
}

const ModelInstance* Project::find_instance_by_id(size_t obj_id, size_t inst_id) const
{
    const auto* obj = find_object_by_id(obj_id);
    if (obj == nullptr)
        return nullptr;
    return find_by_id<ModelInstance>(obj->instances, inst_id);
}

const ModelInstance* Project::find_instance_by_id(size_t inst_id) const
{
    for (auto& obj : m_model->objects) {
        if (auto* ret = find_by_id<ModelInstance>(obj->instances, inst_id); ret != nullptr) {
            return ret;
        }
    }
    return nullptr;
}

Domain::ModelObject* Project::find_object_by_id(size_t id)
{
    return find_by_id<Domain::ModelObject>(m_model->objects, id);
}

Domain::ModelVolume* Project::find_volume_by_id(size_t obj_id, size_t vol_id)
{
    auto* obj = find_object_by_id(obj_id);
    if (obj == nullptr)
        return nullptr;
    return find_by_id<Domain::ModelVolume>(obj->volumes, vol_id);
}

ModelInstance* Project::find_instance_by_id(size_t obj_id, size_t inst_id)
{
    auto* obj = find_object_by_id(obj_id);
    if (obj == nullptr)
        return nullptr;
    return find_by_id<ModelInstance>(obj->instances, inst_id);
}

ModelInstance* Project::find_instance_by_id(size_t inst_id)
{
    for (auto& obj : m_model->objects) {
        if (auto* ret = find_by_id<ModelInstance>(obj->instances, inst_id); ret != nullptr) {
            return ret;
        }
    }
    return nullptr;
}

const BedInstance* Project::find_bed_instance_by_id(size_t id) const
{
    for (const auto& cc : m_config_containers)
        if (auto* bed_inst = find_by_id(cc->bed_instances(), id))
            return bed_inst;
    return nullptr;
}

BedInstance* Project::find_bed_instance_by_id(size_t id)
{
    for (const auto& cc : m_config_containers)
        if (auto* bed_inst = find_by_id(cc->bed_instances(), id))
            return bed_inst;
    return nullptr;
}

const Bed* Project::find_bed_by_id(size_t id) const
{
    return find_by_id(m_bed_container.beds(), id);
}

Bed* Project::find_bed_by_id(size_t id)
{
    return find_by_id(m_bed_container.beds(), id);
}

const ConfigContainer* Project::find_config_container_by_element(const ElementRef& element) const
{
    if (element.is_wipe_tower()) {
        return this->find_config_container_by_bed_instance_id(
            element.wipe_tower_id.bed_instance_id
        );
    }

    const ModelInstance* model_instance =
        this->find_instance_by_id(element.object_id, element.instance_id);
    if (model_instance == nullptr) {
        return nullptr;
    }

    return this->find_config_container(model_instance->get_last_bed().config_container_id);
}

ConfigContainer* Project::find_config_container_by_element(const ElementRef& element)
{
    if (element.is_wipe_tower()) {
        return this->find_config_container_by_bed_instance_id(
            element.wipe_tower_id.bed_instance_id
        );
    }

    ModelInstance* model_instance =
        this->find_instance_by_id(element.object_id, element.instance_id);
    if (model_instance == nullptr) {
        return nullptr;
    }

    return find_config_container(model_instance->get_last_bed().config_container_id);
}

namespace {

void visit(
    Project::ConfigContainerList& ccs,
    const std::function<void(Preset::SelectedPreset&)>& visitor
)
{
    std::ranges::for_each(
        ccs,
        [&](auto& cc)
        {
            auto& selected_preset = cc->mutable_selected_preset();
            visitor(selected_preset);
        }
    );
}

template <Preset::ConfigBoxLike Fdm, Preset::ConfigBoxLikeOrMonostate Sla>
struct PresetModifier
{
    using EP              = Preset::EvaluatedPreset<Fdm, Sla>;
    using Selector        = std::function<EP&(Preset::SelectedPreset& preset)>;
    using MultiSelector   = std::function<std::vector<EP>&(Preset::SelectedPreset& preset)>;
    using SelectorVariant = std::variant<Selector, MultiSelector>;

    const EP& source;
    SelectorVariant selector;

    void operator()(Preset::SelectedPreset& preset) const
    {
        std::visit(
            overloaded{
                [&preset, this](const Selector& sel)
                {
                    EP& p = sel(preset);
                    if (p.id == source.id) {
                        p = source;
                    }
                },
                [&preset, this](const MultiSelector& sel)
                {
                    std::vector<EP>& ps = sel(preset);
                    std::ranges::for_each(
                        ps,
                        [&](auto& p)
                        {
                            if (p.id == source.id) {
                                p = source;
                            }
                        }
                    );
                }
            },
            selector
        );
    }
};

template <Preset::ConfigBoxLike Fdm, Preset::ConfigBoxLikeOrMonostate Sla>
PresetModifier<Fdm, Sla> make_preset_modifier(
    const Preset::EvaluatedPreset<Fdm, Sla>& source,
    const typename PresetModifier<Fdm, Sla>::SelectorVariant& selector
)
{
    PresetModifier<Fdm, Sla> modifier{source, selector};
    return modifier;
}

} // namespace

void Project::update_preset(const Preset::EvaluatedPrinterPreset::Preset& printer)
{
    visit(
        m_config_containers,
        make_preset_modifier(
            printer,
            [](Preset::SelectedPreset& preset) -> auto& { return preset.printer; }
        )
    );
}

void Project::update_preset(const Preset::EvaluatedPrintPreset::Preset& print)
{
    visit(
        m_config_containers,
        make_preset_modifier(
            print,
            [](Preset::SelectedPreset& preset) -> auto& { return preset.print; }
        )
    );
}

void Project::update_preset(const Preset::EvaluatedToolPrintPreset::Preset& tool_print)
{
    visit(
        m_config_containers,
        make_preset_modifier(
            tool_print,
            [](Preset::SelectedPreset& preset) -> auto& { return preset.tools; }
        )
    );
}

void Project::update_preset(const Preset::EvaluatedMaterialPreset::Preset& material)
{
    visit(
        m_config_containers,
        make_preset_modifier(
            material,
            [](Preset::SelectedPreset& preset) -> auto& { return preset.materials; }
        )
    );
}

} // namespace Slic3r::Domain
