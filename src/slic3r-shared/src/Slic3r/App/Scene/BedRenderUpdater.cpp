#include "Slic3r/App/Scene/BedRenderUpdater.hpp"
#include "Slic3r/App/Scene/BedNodeTag.hpp"
#include "Slic3r/App/Scene/BedMaterials.hpp"
#include "Slic3r/App/Scene/NodeVisitor.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/Domain/Bed.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/Workbench.hpp"
#include "Slic3r/Domain/Types.hpp"

#include "Slic3r/Assert.hpp"

using Slic3r::Domain::Transform3d;

namespace Slic3r::App::Scene {

using Domain::BedRef;

namespace {

void apply_virtual_bed_style(Node& n, Render::Device& device)
{
    BedNodeTag* bt = n.tag_of_type<BedNodeTag>();
    if (bt == nullptr || bt->type == BedElementType::Undefined)
        return;
    ASSERT(bt->is_virtual);
    if (!n.has_render_component())
        return;

    const Render::Material& primary = n.render_component()->material();
    std::optional<Render::Material> material;
    switch (bt->type) {
    case BedElementType::Model: material = BedMaterials::model_unselected_material(primary); break;
    case BedElementType::PlateDefault: material = BedMaterials::plate_default_unselected_material(primary); break;
    case BedElementType::PlateTextured: material = BedMaterials::plate_textured_transparent_material(primary); break;
    case BedElementType::Grid: material = BedMaterials::grid_unselected_material(primary); break;
    default:
        // Everything else shall be hidden for virtual bed.
        n.set_enabled(false);
        break;
    }
    if (material.has_value())
        n.set_material_override(*material);
}

} // namespace

void BedRenderUpdater::update_materials(const BedError& bed_error)
{
    m_bed_error = bed_error;

    visit(m_scene_provider.scene().root(), [&](Node& n) {
        BedNodeTag* tag = n.tag_of_type<BedNodeTag>();
        if (tag != nullptr && tag->type != BedElementType::Undefined) {
            if (tag->is_virtual) {
                apply_virtual_bed_style(n, m_device);
                return;
            }
            ASSERT(m_project != nullptr);
            const Domain::ConfigContainer* cc = m_project->find_config_container(tag->config_container_id);
            ASSERT(cc != nullptr);
            const Domain::BedInstance* inst = Domain::find_by_id(cc->bed_instances(), tag->instance_id);
            if (inst == nullptr)
                return;

            bool has_error = m_bed_error.contains(Domain::SlicingId{m_project_id, tag->instance_id});

            if (m_scene_interactor.bed_selection().is_selected(BedRef{tag->config_container_id, tag->instance_id})) {
                std::optional<Render::Material> material;
                if (has_error) {
                    switch (tag->type)
                    {
                    case BedElementType::PlateDefault:  { material = BedMaterials::plate_default_error_material(n.render_component()->material()); break; }
                    case BedElementType::PlateTextured: { material = BedMaterials::plate_textured_error_material(n.render_component()->material()); break; }
                    case BedElementType::Model:         { material = BedMaterials::model_error_material(n.render_component()->material()); break; }
                    default: { break; }
                    }
                }

                if (material.has_value())
                    n.set_material_override(*material);
                else
                    n.remove_material_override();

                if (tag->type == BedElementType::Label && m_scene_interactor.bed_selection().last_selected_bed() != 
                    BedRef{ tag->config_container_id, tag->instance_id }) {
                    n.set_material_override(
                        BedMaterials::label_secondary_selection_material(n.render_component()->material(), m_device, inst->label())
                    );
                }
            }
            else {
                std::optional<Render::Material> material;
                switch (tag->type)
                {
                case BedElementType::PlateDefault:
                {
                    material = has_error ? 
                        BedMaterials::plate_default_unselected_error_material(n.render_component()->material()) :
                        BedMaterials::plate_default_unselected_material(n.render_component()->material());
                    break;
                }
                case BedElementType::Model:
                {
                    material = has_error ?
                        BedMaterials::model_unselected_error_material(n.render_component()->material()) :
                        BedMaterials::model_unselected_material(n.render_component()->material());
                    break;
                }
                case BedElementType::PlateTextured: { material = BedMaterials::plate_textured_transparent_material(n.render_component()->material()); break; }
                case BedElementType::Contour:       { material = BedMaterials::contour_unselected_material(n.render_component()->material()); break; }
                case BedElementType::Grid:          { material = BedMaterials::grid_unselected_material(n.render_component()->material()); break; }
                case BedElementType::PrintVolume:   { material = BedMaterials::print_volume_unselected_material(n.render_component()->material()); break; }
                case BedElementType::Label:         { material = BedMaterials::label_unselected_material(n.render_component()->material(), m_device, inst->label()); break; }
                default:                            { break; }
                }

                if (material.has_value())
                    n.set_material_override(*material);
            }
        }
    }, true);
}

void BedRenderUpdater::update_shadows(const Camera& cam)
{
    bool cam_pointing_upward = cam.pointing_upward();

    visit(m_scene_provider.scene().root(), [&](Node& n) {
        BedNodeTag* tag = n.tag_of_type<BedNodeTag>();
        if (tag != nullptr) {
            if (tag->is_virtual) {
                return;
            }
            if (tag->type == BedElementType::Model ||
                tag->type == BedElementType::PlateDefault ||
                tag->type == BedElementType::PlateTextured) {

                DEBUG_ASSERT(n.has_render_component());

                const Domain::ConfigContainer* cc = m_project->find_config_container(tag->config_container_id);
                const Domain::BedInstance* inst = Domain::find_by_id(cc->bed_instances(), tag->instance_id);
                if (inst == nullptr)
                    return;

                if (!cam_pointing_upward &&
                    m_scene_interactor.bed_selection().is_selected(BedRef{ tag->config_container_id, tag->instance_id })) {
                    if (tag->type == BedElementType::Model && Scene::Scene::graphics_settings().bed_model_cast_shadow())
                        n.render_component()->set_shadows(Render::Shadows{ true, true });
                    else
                        n.render_component()->set_shadows(Render::Shadows{ false, true });
                }
                else
                    n.render_component()->set_shadows(Render::Shadows{ false, false });
            }
        }
    }, true);
}

void BedRenderUpdater::update_positions()
{
    visit(m_scene_provider.scene().root(), [this](Node& n) {
        BedNodeTag* tag = n.tag_of_type<BedNodeTag>();
        if (tag != nullptr) {
            if (tag->is_virtual) {
                return;
            }
            if (tag->type == BedElementType::Undefined) {
                DEBUG_ASSERT(m_project != nullptr);
                const Domain::ConfigContainer* cc = m_project->find_config_container(tag->config_container_id);
                DEBUG_ASSERT(cc != nullptr);
                const Domain::BedInstance* inst = Domain::find_by_id(cc->bed_instances(), tag->instance_id);
                if (inst == nullptr)
                    return;

                n.set_world_transform(inst->matrix());
            }
        }
    }, true);
}

void BedRenderUpdater::update_elements_state()
{
    size_t bed_instances_count = 0;

    visit(
        m_scene_provider.scene().root(),
        [&](Node& n) {
            BedNodeTag* tag = n.tag_of_type<BedNodeTag>();
            if (tag == nullptr)
                return;
            if (tag->is_virtual) {
                return;
            }
            if (tag->type == BedElementType::Undefined) {
                ++bed_instances_count;
                return;
            }
            if (tag->type != BedElementType::Contour &&
                tag->type != BedElementType::PrintVolume &&
                tag->type != BedElementType::AxesMain)
                return;

            ASSERT(m_project != nullptr);
            const Domain::ConfigContainer* cc = m_project->find_config_container(tag->config_container_id);
            ASSERT(cc != nullptr);
            const Domain::BedInstance* inst = Domain::find_by_id(cc->bed_instances(), tag->instance_id);
            if (inst == nullptr)
                return;

            BedRef bed_ref{tag->config_container_id, tag->instance_id};
            // update elements' visibility
            switch (tag->type) {
            case BedElementType::Contour: {
                n.set_enabled(bed_ref == m_scene_interactor.bed_selection().last_selected_bed());
                break;
            }
            case BedElementType::PrintVolume: {
                n.set_enabled(inst->print_volume_enabled);
                break;
            }
            case BedElementType::AxesMain: {
                n.set_enabled(m_scene_interactor.bed_selection().is_selected(bed_ref));
                break;
            }
            default:
                break;
            }
        },
        true
    );

    visit(
        m_scene_provider.scene().root(),
        [&](Node& n) {
            BedNodeTag* tag = n.tag_of_type<BedNodeTag>();
            if (tag != nullptr && !tag->is_virtual && tag->type == BedElementType::Label)
                n.set_enabled(bed_instances_count > 1);
        },
        true
    );
}

void BedRenderUpdater::camera_updated(const Camera& cam)
{
    bool cam_pointing_upward = cam.pointing_upward();
    auto& scene = m_scene_provider.scene();
    visit(scene.root(), [&](Node& n) {
        BedNodeTag* tag = n.tag_of_type<BedNodeTag>();
        if (tag != nullptr) {
            if (tag->is_virtual) {
                return;
            }
            if (tag->type == BedElementType::Model)
                // turn beds' model visibility on/off in dependence of camera position/orientation
                n.set_enabled(!cam_pointing_upward);
            else if (tag->type == BedElementType::PlateDefault) {
                // change material in dependence of camera position/orientation
                if (cam_pointing_upward)
                    n.set_material_override(BedMaterials::plate_default_transparent_material(n.render_component()->material()));
                else if (m_bed_error.contains(Domain::SlicingId{ m_project_id, tag->instance_id }))
                    n.set_material_override(BedMaterials::plate_default_error_material(n.render_component()->material()));
                else if (!m_scene_interactor.bed_selection().is_selected(BedRef{ tag->config_container_id, tag->instance_id }))
                    n.set_material_override(BedMaterials::plate_default_unselected_material(n.render_component()->material()));
                else
                    n.remove_material_override();
            }
            else if (tag->type == BedElementType::PlateTextured) {
                // change material in dependence of camera position/orientation
                DEBUG_ASSERT(m_project != nullptr);
                const Domain::ConfigContainer* cc = m_project->find_config_container(tag->config_container_id);
                DEBUG_ASSERT(cc != nullptr);
                const Domain::BedInstance* inst = Domain::find_by_id(cc->bed_instances(), tag->instance_id);
                if (inst == nullptr)
                    return;

                if (cam_pointing_upward ||
                    !m_scene_interactor.bed_selection().is_selected(BedRef{ tag->config_container_id, tag->instance_id }))
                    n.set_material_override(BedMaterials::plate_textured_transparent_material(n.render_component()->material()));
                else {
                    if (m_bed_error.contains(Domain::SlicingId{ m_project_id, tag->instance_id }))
                        n.set_material_override(BedMaterials::plate_textured_error_material(n.render_component()->material()));
                    else
                        n.remove_material_override();
                }
            }
            else if (tag->type == BedElementType::AxesScaler) {
                // change axes scale in dependence of camera zoom
                DEBUG_ASSERT(m_project != nullptr);
                const Domain::ConfigContainer* cc = m_project->find_config_container(tag->config_container_id);
                DEBUG_ASSERT(cc != nullptr);
                const Domain::BedInstance* inst = Domain::find_by_id(cc->bed_instances(), tag->instance_id);
                if (inst == nullptr)
                    return;

                if (m_scene_interactor.bed_selection().is_selected(BedRef{ tag->config_container_id, tag->instance_id })) {
                    Transform3d scale = Transform3d::Identity();
                    scale.scale(std::min(1.0, 1.0 / cam.zoom() * 10.0));
                    n.set_local_transform(scale);
                }
            }
        }
    }, true);

    update_shadows(cam);
}

void BedRenderUpdater::on_selected_project_changed(Domain::SelectionId project_id)
{
    m_project = &m_workbench.project(project_id);
    m_project_id = project_id;
}

} // namespace Slic3r::App::Scene
