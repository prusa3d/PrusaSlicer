#include "Slic3r/App/Plater/ArrangeGizmo.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Scene/ISceneProvider.hpp"
#include "Slic3r/App/Scene/GeometryDataFactory.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Biz/ArrangeInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Domain/Workbench.hpp"

namespace Slic3r::App::Plater {

using Biz::Algorithms::Scaling::scaled;
using Biz::Arrange::Settings;
using Biz::Scene::BedSelection;
using Domain::Bed;
using Domain::BedSegments;
using Domain::BedRefs;
using Domain::coord_t;
using Domain::Project;
using Domain::SelectionId;
using Domain::Vec2f;
using Yoga::ItemPtr;
using NodeList = Scene::Node::NodeList;
using Biz::Platform::PlatformServices;
using Biz::Platform::JobManager::JobManagerStatus;
using Biz::Platform::JobManager::Progress;
using Domain::ConfigContainer;
using Domain::JobStatus;

namespace {

const Bed* get_bed(const Project& project, const BedSelection& selection) {
    const Bed* bed{nullptr};
    for (const Domain::BedRef& bed_ref : selection.selected_beds()) {
        const Domain::BedInstance* bed_instance{project.find_bed_instance_by_id(bed_ref.instance_id)};
        if (!bed_instance) {
            return nullptr;
        }
        if (!bed) {
            bed = &bed_instance->bed.get();
        } else {
            if (*bed != bed_instance->bed) {
                return nullptr;
            }
        }
    }

    return bed;
}

std::optional<BedSegments> get_bed_segments(const Project& project, const BedSelection& selection)
{
    const Bed* bed{get_bed(project, selection)};
    if (!bed) {
        return std::nullopt;
    }
    return bed->segments();
}

std::optional<Domain::Vec2d>
get_auxiliary_travel_anchor(const Project& project, const BedSelection& selection)
{
    const Bed* bed{get_bed(project, selection)};
    if (!bed) {
        return std::nullopt;
    }
    return bed->auxiliary_travel_anchor();
}
} // namespace

ArrangeGizmo::ArrangeGizmo(
    Biz::ArrangeInteractor& arrange_interactor,
    Render::Device& device,
    Scene::ISceneProvider& scene_provider,
    Scene::GeometryDataFactory& data_factory,
    Biz::ProjectInteractor& project_interactor,
    const Domain::Workbench& workbench
) :
    m_arrange_interactor{arrange_interactor},
    m_device{device},
    m_scene_provider{scene_provider},
    m_data_factory{data_factory},
    m_project_interactor{project_interactor},
    m_workbench{workbench},
    m_dialog(
        std::make_unique<ArrangeDialog>(
            [this](ArrangeDialog::ArrangeMode mode) {
                if (mode == ArrangeDialog::ArrangeMode::PrinterGroup) {
                    arrange_selected_config_container();
                } else {
                    arrange_selected_beds();
                }
            },
            []() { PlatformServices::instance().job_manager().cancel_job("arrange"); },
            default_settings()
        )
    )
{
    m_project_interactor.scene_interactor().add_listener<Biz::ISelectedBedInstancesChangedListener>(this);
    m_project_interactor.preset_interactor().add_listener<Biz::Preset::IPresetChangedListener>(this);
    m_project_interactor.slicing_interactor().add_listener<Biz::Slicing::IWipeTowerGeometryListener>(this);

    PlatformServices::instance().job_manager().add_listener<Biz::Platform::JobManager::IJobManagerStatusChangedListener>(this);
}

ArrangeGizmo::~ArrangeGizmo()
{
    m_project_interactor.scene_interactor()
        .remove_listener<Biz::ISelectedBedInstancesChangedListener>(this);
    m_project_interactor.preset_interactor().remove_listener<Biz::Preset::IPresetChangedListener>(
        this);
    m_project_interactor.slicing_interactor().remove_listener<Biz::Slicing::IWipeTowerGeometryListener>(this);
    PlatformServices::instance().job_manager().remove_listener<Biz::Platform::JobManager::IJobManagerStatusChangedListener>(this);
}

Scene::GizmoActivationState ArrangeGizmo::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
{
    return Scene::GizmoActivationState::Inactive;
};

void ArrangeGizmo::on_selected_bed_instances_changed(SelectionId, const BedSelection&)
{
    update_dialog();
};

void ArrangeGizmo::on_preset_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Biz::Preset::PresetItemType type)
{
    update_dialog();
}

void ArrangeGizmo::on_wipe_tower_geometry_changed(Biz::Slicing::OptWipeTowerGeometry g,
                                                  const Domain::SlicingId)
{
    update_dialog();
}

void ArrangeGizmo::update_dialog() {
    const Project& project{m_workbench.project(m_project_interactor.selected_project_id())};
    const BedSelection& bed_selection{m_project_interactor.scene_interactor().bed_selection()};
    const std::optional<BedSegments> bed_segments{get_bed_segments(project, bed_selection)};
    const std::optional<Domain::Vec2d> auxiliary_travel_anchor{
        get_auxiliary_travel_anchor(project, bed_selection)
    };
    m_dialog->set_bed_segments(bed_segments);
    if (m_project_interactor.scene_interactor().wipe_tower_geometry(
            bed_selection.last_selected_bed().instance_id))
    {
        m_dialog->set_auxiliary_travel_anchor(auxiliary_travel_anchor);
    } else {
        m_dialog->set_auxiliary_travel_anchor(std::nullopt);
    }
}

void ArrangeGizmo::on_job_manager_status_changed(const Biz::Platform::JobManager::JobManagerStatus& status)
{
    const auto it{status.find("arrange")};
    if (it == status.end()) {
        m_dialog->update_status(ArrangeTaskStatus::Idle);
        return;
    }

    const Progress progress{it->second};
    if (progress.status == JobStatus::Started) {
        m_dialog->update_status(ArrangeTaskStatus::Running);
        return;
    }
    m_dialog->update_status(ArrangeTaskStatus::Idle);
}

void ArrangeGizmo::on_activated()
{
    m_active = true;
};

void ArrangeGizmo::on_deactivated()
{
    m_active = false;
};

void ArrangeGizmo::register_commands(Platform::CommandRegistry& registry) {
    registry
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "arrange-gizmo-arrange",
                [this]() {
                    arrange_selected_config_container();
                },
                Platform::FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::A}
                        }
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "arrange-gizmo-arrange-selection",
                [this]() {
                    arrange_selection_in_selected_config_container();
                },
                Platform::FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{
                                Platform::KeyModifiers(Platform::KeyModifier::Shift),
                                Platform::KeyCode::A
                            }
                        }
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "arrange-gizmo-arrange-local",
                [this]() {
                    arrange_selected_beds();
                },
                Platform::FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::D}
                        }
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                "arrange-gizmo-arrange-local-selection",
                [this]() {
                    arrange_selection_on_selected_beds();
                },
                Platform::FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyModifiers(Platform::KeyModifier::Shift),
                            Platform::KeyboardShortcut{0, Platform::KeyCode::D}
                        }
                }
            )
        );
}

Scene::ToolType ArrangeGizmo::type() const
{
    return Scene::ToolType::ArrangeGizmo;
}

bool ArrangeGizmo::enabled() const
{
    return true;
}

GizmoWindowPtr ArrangeGizmo::release_ui_window()
{
    return m_dialog.release();
}

Settings ArrangeGizmo::default_settings() const
{
    Settings settings;
    settings.scaled_offset = scaled(3.0);

    const Project& project{m_project_interactor.selected_project()};
    const BedSelection& bed_selection{m_project_interactor.scene_interactor().bed_selection()};
    const std::optional<BedSegments> bed_segments{get_bed_segments(project, bed_selection)};

    settings.bed_segments = bed_segments;
    return settings;
}

static void append_printable_instances(Domain::ConstModelInstanceList& destination, const Domain::ModelInstanceList& source) {
    for (const Domain::ModelInstance* instance : source) {
        if (instance->printable) {
            destination.push_back(instance);
        }
    }
}

void ArrangeGizmo::arrange_selected_config_container()
{
    const ConfigContainer& config_container{m_project_interactor.selected_config_container()};

    Domain::ConstModelInstanceList instances{};
    std::vector<Biz::BedToArrange> beds;
    for (const auto& bed_instance : config_container.bed_instances()) {
        append_printable_instances(instances, bed_instance->model_instances);
        beds.push_back(
            {Domain::BedRef{config_container.id().id, bed_instance->id().id},
             bed_instance->index()});
    }

    const Domain::ModelInstanceList& unplaced_instances{
        m_project_interactor.scene_interactor().unplaced_model_instances(
            m_project_interactor.selected_project_id())};
    append_printable_instances(instances, unplaced_instances);

    m_arrange_interactor.arrange(
        m_project_interactor.selected_project_id(),
        beds,
        config_container.id().id,
        instances,
        m_dialog->get_settings(),
        [this]()
        { m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::Arrange); });
}

void ArrangeGizmo::arrange_selection_in_selected_config_container()
{
    const Domain::Project& project{
        m_project_interactor.workbench().project(m_project_interactor.selected_project_id())};

    const ConfigContainer& config_container{m_project_interactor.selected_config_container()};

    const Biz::Scene::ObjectSelection& selection{
        m_project_interactor.scene_interactor().object_selection()};

    std::vector<Biz::BedToArrange> beds;
    for (const auto& bed_instance : config_container.bed_instances()) {
        Biz::BedToArrange
            bed{{config_container.id().id, bed_instance->id().id}, bed_instance->index()};

        const Domain::ElementRef wipe_tower_ref{
            Domain::SlicingId{m_project_interactor.selected_project_id(), bed_instance->id().id}
        };

        bed.fixed_wipe_tower = !selection.is_selected(wipe_tower_ref);

        for (const Domain::ModelInstance* instance : bed_instance->model_instances) {
            const Domain::ElementRef instance_ref{
                instance->get_object()->id().id, instance->id().id
            };
            if (!selection.is_selected(instance_ref)) {
                bed.fixed.push_back(instance);
            }
        }
        beds.push_back(bed);
    }

    Domain::ConstModelInstanceList instances;
    for (const Domain::ElementRef& instance_ref : selection.elements) {
        const Domain::ModelInstance* instance{
            project.find_instance_by_id(instance_ref.object_id, instance_ref.instance_id)};
        if (!instance) {
            continue;
        }
        instances.push_back(instance);
    }

    m_arrange_interactor.arrange(
        m_project_interactor.selected_project_id(),
        beds,
        config_container.id().id,
        instances,
        m_dialog->get_settings(),
        [this]()
        { m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::Arrange); });
}

void ArrangeGizmo::arrange_selected_beds()
{
    const Domain::Project& project{
        m_project_interactor.workbench().project(m_project_interactor.selected_project_id())};

    const Domain::BedRefs beds{
    m_project_interactor.scene_interactor().bed_selection().selected_beds()};

    std::vector<Biz::BedToArrange> beds_to_arrange;
    for (const Domain::BedRef& bed_ref : beds) {
        Biz::BedToArrange bed_to_arrange;
        bed_to_arrange.ref = bed_ref;

        const Domain::BedInstance* bed_instance{
            project.find_bed_instance_by_id(bed_ref.instance_id)};

        if (!bed_instance) {
            continue;
        }

        bed_to_arrange.index = bed_instance->index();

        append_printable_instances(bed_to_arrange.arrangeable, bed_instance->model_instances);
        beds_to_arrange.push_back(bed_to_arrange);
    }

    m_arrange_interactor.arrange(
        m_project_interactor.selected_project_id(),
        beds_to_arrange,
        std::nullopt,
        {},
        m_dialog->get_settings(),
        [this]()
        { m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::Arrange); });
}

void ArrangeGizmo::arrange_selection_on_selected_beds()
{
    const Domain::Project& project{
        m_project_interactor.workbench().project(m_project_interactor.selected_project_id())};


    const Biz::Scene::ObjectSelection& selection{
        m_project_interactor.scene_interactor().object_selection()};

    const Domain::BedRefs beds{
        m_project_interactor.scene_interactor().bed_selection().selected_beds()};

    std::set<const Domain::ModelInstance*> on_bed;
    std::vector<Biz::BedToArrange> beds_to_arrange;
    for (const Domain::BedRef& bed_ref : beds) {
        Biz::BedToArrange bed_to_arrange;
        bed_to_arrange.ref = bed_ref;
        const Domain::ElementRef wipe_tower_ref{
            Domain::SlicingId{m_project_interactor.selected_project_id(), bed_ref.instance_id}
        };
        bed_to_arrange.fixed_wipe_tower = !selection.is_selected(wipe_tower_ref);

        const Domain::BedInstance* bed_instance{
            project.find_bed_instance_by_id(bed_ref.instance_id)};

        if (!bed_instance) {
            continue;
        }

        bed_to_arrange.index = bed_instance->index();

        for (const Domain::ModelInstance* instance : bed_instance->model_instances) {
            const Domain::ElementRef instance_ref{
                instance->get_object()->id().id, instance->id().id
            };
            if (!selection.is_selected(instance_ref)) {
                bed_to_arrange.fixed.push_back(instance);
            } else {
                bed_to_arrange.arrangeable.push_back(instance);
                on_bed.insert(instance);
            }
        }

        beds_to_arrange.push_back(bed_to_arrange);
    }

    Domain::ConstModelInstanceList extra;
    for (const Domain::ElementRef& instance_ref : selection.elements) {
        const Domain::ModelInstance* instance{
            project.find_instance_by_id(instance_ref.object_id, instance_ref.instance_id)};
        if (!instance || on_bed.contains(instance)) {
            continue;
        }
        extra.push_back(instance);
    }

    m_arrange_interactor.arrange(
        m_project_interactor.selected_project_id(),
        beds_to_arrange,
        std::nullopt,
        extra,
        m_dialog->get_settings(),
        [this]()
        { m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::Arrange); });
}

} // namespace Slic3r::App::Plater
