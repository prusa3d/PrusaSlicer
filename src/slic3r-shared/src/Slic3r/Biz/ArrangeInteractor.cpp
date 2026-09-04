#include "Slic3r/Biz/ArrangeInteractor.hpp"
#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Biz/Arrange/ArrangeItem.hpp"
#include "Slic3r/Biz/Arrange/Arrange.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Domain/ConfigDefsFDM.hpp"

#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp" // project_mesh
#include <ranges>

namespace Slic3r::Biz {

using Algorithms::BoundingBox::merge;
using Algorithms::BoundingBox::to_2d;
using Algorithms::ClipperUtils::union_ex;
using Algorithms::ExPolygon::area;
using Algorithms::Point::to_2d;
using Algorithms::Scaling::scaled;
using Algorithms::Scaling::unscaled;
using Arrange::ArbitraryShape;
using Arrange::ArrangeItem;
using Arrange::ArrangeResult;
using Arrange::InputShape;
using Arrange::Settings;
using Arrange::StopCondition;
using Arrange::to_arrange_items;
using Biz::JThread::StopToken;
using Domain::BedInstance;
using Domain::BedRef;
using Domain::BedRefs;
using Domain::BoundingBox2crd;
using Domain::BoundingBox2d;
using Domain::BoundingBox3d;
using Domain::ConfigContainer;
using Domain::ConfigItem;
using Domain::ConfigPack;
using Domain::ConfigPackFDM;
using Domain::ConstModelInstanceList;
using Domain::ElementRef;
using Domain::ElementRefs;
using Domain::ExPolygons;
using Domain::ModelInstance;
using Domain::ModelInstanceList;
using Domain::ModelVolumeType;
using Domain::Points;
using Domain::Polygon;
using Domain::Polygons;
using Domain::Project;
using Domain::SelectionId;
using Domain::Transform3d;
using Domain::Transformation;
using Domain::TriangleMesh;
using Domain::Vec2crd;
using Domain::Vec2d;
using Domain::Vec2ds;
using Domain::Vec3d;
using Domain::Workbench;
using Platform::PlatformServices;
using Platform::JobManager::JobManager;
using Platform::JobManager::ProgressTracker;
using Scene::BedInstances;

using Trafo  = Arrange::InstanceTransform2D;
using Trafos = Arrange::InstanceTransforms;

namespace BB = Biz::Algorithms::BoundingBox;

namespace {

BoundingBox3d instance_bounding_box(const ModelInstance& instance)
{
    Domain::BoundingBox3d result;
    const Transform3d inst_matrix{instance.get_transformation().get_matrix()};

    for (const Domain::ModelVolume* volume : instance.get_object()->volumes) {
        if (!volume->is_model_part()) {
            continue;
        }
        result = merge(
            result,
            Algorithms::BoundingBox::transformed(
                volume->mesh().bounding_box(),
                inst_matrix * volume->get_matrix()
            )
        );
    }

    return result;
}

constexpr double UnscaledCoordLimit = 1000.;

bool check_coord_bounds(const BoundingBox2d& bb)
{
    const Vec2d bb_size{Algorithms::BoundingBox::sizes(bb)};
    return std::abs(bb_size.x()) < UnscaledCoordLimit && std::abs(bb_size.y()) < UnscaledCoordLimit;
}

using TriangleMeshPtr = std::shared_ptr<const TriangleMesh>;

struct InstanceMesh
{
    TriangleMeshPtr mesh_ptr;
    Transform3d trafo;
    ModelVolumeType type;
};

struct InstanceMeshes
{
    Domain::ElementRef element_ref;
    std::vector<InstanceMesh> meshes;
};

struct CanceledException : public std::runtime_error
{
    CanceledException() : std::runtime_error{"canceled"} {}
};

std::optional<ArbitraryShape> extract_outline(const InstanceMeshes& meshes, const StopCondition stop_condition)
{
    ArbitraryShape result;

    const auto throw_on_cancel{[stop_condition]() {
        if (stop_condition()) {
            throw CanceledException{};
        }
    }};

    for (const InstanceMesh& mesh : meshes.meshes) {
        try {
            if (mesh.type != ModelVolumeType::MODEL_PART) {
                continue;
            }
            const Polygons vol_outline{project_mesh(mesh.mesh_ptr->its, mesh.trafo, stop_condition, 0.0)};
            result = union_ex(result, vol_outline);
        } catch (const CanceledException&) {
            return std::nullopt;
        }
    }

    return result;
}

Points get_bed_contour(const ArrangeBed& arrange_bed, const Settings& settings)
{
    const Domain::Bed& bed{arrange_bed.bed_instance->bed.get()};
    Points points;
    for (const Vec2d& point : bed.contour()) {
        points.push_back(scaled(point));
    }

    const int offset{
        static_cast<int>(settings.scaled_offset)
        - scaled(arrange_bed.offset)
        - scaled(0.1) // Add 0.1 mm safety offset from the bed boundary.
    };
    const Polygons offset_contour{Algorithms::ClipperUtils::offset({Polygon{points}}, offset)};

    if (offset_contour.size() != 1) {
        return points;
    }

    return offset_contour.front().points;
}

enum class ResetTranslation
{
    True,
    False
};

using MeshTranslation = std::variant<ResetTranslation, Vec3d>;

std::vector<InstanceMeshes> get_meshes(
    const ConstModelInstanceList& instances,
    const MeshTranslation translation
)
{
    std::vector<InstanceMeshes> result;
    result.reserve(instances.size());
    for (const ModelInstance* instance : instances) {
        std::vector<InstanceMesh> meshes;
        for (const Domain::ModelVolume* volume : instance->get_object()->volumes) {
            Transform3d instance_trafo{instance->get_matrix()};

            if (std::holds_alternative<ResetTranslation>(translation)) {
                const auto reset_translation{std::get<ResetTranslation>(translation)};
                if (reset_translation == ResetTranslation::True) {
                    instance_trafo.translation().x() = 0;
                    instance_trafo.translation().y() = 0;
                }
            } else {
                const auto translation_vector{std::get<Vec3d>(translation)};
                instance_trafo.translation() += translation_vector;
            }

            InstanceMesh mesh{
                .mesh_ptr = volume->mesh_ptr(),
                .trafo    = instance_trafo * volume->get_matrix(),
                .type     = volume->type(),
            };

            meshes.push_back(std::move(mesh));
        }

        const ElementRef instance_ref{instance->get_object()->id().id, instance->id().id, 0};

        result.push_back(
            InstanceMeshes{
                .element_ref = instance_ref,
                .meshes      = std::move(meshes),
            }
        );
    }
    return result;
}

std::optional<std::vector<InputShape>> get_arrange_input(
    const std::vector<InstanceMeshes>& meshes,
    const StopCondition stop_condition
)
{
    std::vector<InputShape> result;
    result.reserve(meshes.size());
    for (const InstanceMeshes& instance : meshes) {
        const std::optional<ArbitraryShape> outline{extract_outline(instance, stop_condition)};
        if (!outline) {
            return std::nullopt;
        }
        if (outline->empty()) {
            continue;
        }
        result.push_back({instance.element_ref, *outline});
    }

    std::ranges::stable_sort(result, [](const InputShape& a, const InputShape& b) {
        const double a_area{area(a.shape)};
        const double b_area{area(b.shape)};

        if (a_area <= 0 || b_area <= 0) {
            return false;
        }

        const double percentage_diff{std::abs(a_area - b_area) / std::min(a_area, b_area)};
        if (percentage_diff < 0.05) {
            // a < b == false && b < a == false => a == b and the order is preserved in stable sort.
            return false;
        }

        return b_area < a_area;
    });

    return result;
}

double get_max_brim(const ConstModelInstanceList& instances, double print_brim_width, bool print_brim_enabled)
{
    double result{0.0};
    for (const ModelInstance* instance : instances) {
        bool object_brim_enabled{false};
        const auto brim_type{
            instance->get_object()->object_settings.overrides.get("brim_type")
        };
        if (brim_type) {
            const auto brim_type_value{brim_type->get<Domain::BrimType>()};
            if (brim_type_value == Domain::BrimType::OuterOnly || brim_type_value == Domain::BrimType::OuterAndInner) {
                object_brim_enabled = true;
            } else {
                // Brim is explicitly disabled for this object, skip it.
                continue;
            }
        } else {
            object_brim_enabled = print_brim_enabled;
        }

        if (!object_brim_enabled) {
            result = std::max(result, print_brim_enabled ? print_brim_width : 0.0);
        } else {
            const std::optional<ConfigItem> brim_width{
                instance->get_object()->object_settings.overrides.get("brim_width")
            };

            if (brim_width) {
                result = std::max(result, brim_width->get<double>());
            } else {
                result = std::max(result, print_brim_width);
            }
        }
    }
    return result;
}

ArrangeBed get_arrange_bed(
    const SelectionId project_id,
    const BedRef& bed_ref,
    const double brim_width,
    const Settings& settings,
    const Workbench& workbench
)
{
    const Project& project{workbench.project(project_id)};
    const ConfigContainer* config_container{project.find_config_container(bed_ref.config_container_id)};

    const BedInstance& bed_instance{
        ASSERT_VAL(config_container)->find_bed_instance(bed_ref.instance_id)
    };
    return {&bed_instance, std::max(brim_width, settings.unscaled_bed_offset)};
}

struct BedWithInstances
{
    BedRef bed_ref;
    std::size_t index{};
    std::vector<InstanceMeshes> arrangeable_instances;
    std::vector<InstanceMeshes> fixed_instances;
    ArrangeBed arrange_bed;
};

using ModelInstancesPerBed = std::vector<BedWithInstances>;

ArrangeItem wipe_tower_to_arrange_item(
    SelectionId project_id,
    const BedRef bed_ref,
    const Slicing::WipeTowerGeometry& wipe_tower,
    const Domain::ModelWipeTower& model_wipe_tower,
    const Settings& settings
)
{
    ArrangeItem result{
        InputShape{
            Domain::ElementRef{Domain::SlicingId{project_id, bed_ref.instance_id}},
            {wipe_tower.get_outline(model_wipe_tower)}
        },
        settings
    };
    result.is_wipe_tower = true;
    result.gravity_sink = settings.auxiliary_travel_anchor;
    return result;
}

struct WipeTowerForArrange
{
    ArrangeItem wipe_tower;
    bool is_fixed{false};
    Vec2crd bed_position{Vec2crd::Zero()};
};

using WipeTowerPerBed = std::map<BedRef, WipeTowerForArrange>;

WipeTowerPerBed get_wipe_towers_per_bed(
    const SelectionId project_id,
    const BedRefs& bed_refs,
    const std::set<BedRef>& fixed_wipe_towers,
    const Settings& settings,
    const Workbench& workbench,
    const Scene::SceneInteractor& scene_interactor)
{
    const Project& project{workbench.project(project_id)};

    WipeTowerPerBed result;
    for (const BedRef& bed_ref : bed_refs) {
        const Slicing::WipeTowerGeometry* wipe_tower_geometry{
            scene_interactor.wipe_tower_geometry(bed_ref.instance_id)};
        if (wipe_tower_geometry == nullptr) {
            continue;
        }

        const BedInstance* bed_instance{project.find_bed_instance_by_id(bed_ref.instance_id)};
        if (!bed_instance) {
            continue;
        }

        const Vec2crd bed_position{scaled(bed_instance->wipe_tower.position)};

        const auto& [it, _]{result.insert({
            bed_ref,
            {
                .wipe_tower = wipe_tower_to_arrange_item(
                    project_id,
                    bed_ref,
                    *wipe_tower_geometry,
                    bed_instance->wipe_tower,
                    settings),
                .is_fixed     = fixed_wipe_towers.contains(bed_ref),
                .bed_position = bed_position,
            },
        })};

        if (it->second.is_fixed) {
            it->second.wipe_tower.set_translation(bed_position);
        }
    }

    return result;
}

ModelInstancesPerBed get_arrange_input_per_bed(
    const SelectionId project_id,
    const std::vector<BedToArrange>& beds,
    const Domain::ConstModelInstanceList& extra,
    const Settings& settings,
    const Workbench& workbench
)
{
    ModelInstancesPerBed result;

    for (const BedToArrange& bed : beds) {
        const ConfigContainer* config_container{workbench.project(project_id).find_config_container(bed.ref.config_container_id)};
        const ConfigPack& config{ASSERT_VAL(config_container)->build_print_config()};

        double print_brim_width{0.0};
        bool print_brim_enabled{false};
        if (std::holds_alternative<ConfigPackFDM>(config)) {
            const ConfigPackFDM& fdm_config{std::get<ConfigPackFDM>(config)};
            const auto brim_type{fdm_config.print.items.opt("brim_type").get<Domain::BrimType>()};
            print_brim_enabled = brim_type == Domain::BrimType::OuterOnly
                || brim_type == Domain::BrimType::OuterAndInner;
            print_brim_width = fdm_config.print.items.opt("brim_width").get<double>();
        }

        const double extra_brim{get_max_brim(extra, print_brim_width, print_brim_enabled)};
        const double arrangeable_brim{
            get_max_brim(bed.arrangeable, print_brim_width, print_brim_enabled)};

        ArrangeBed arrange_bed{get_arrange_bed(
            project_id,
            bed.ref,
            std::max(extra_brim, arrangeable_brim),
            settings,
            workbench)};

        const Vec3d bed_translation{
            arrange_bed.bed_instance->transformation.get_matrix().translation()};
        result.push_back(
            {bed.ref,
             bed.index,
             get_meshes(bed.arrangeable, ResetTranslation::True),
             get_meshes(bed.fixed, -bed_translation),
             std::move(arrange_bed)});
    }

    std::ranges::sort(result, [](const BedWithInstances& a, const BedWithInstances& b){
        return a.index < b.index;
    });

    return result;
}

void offset_trafos(Trafos& trafos, const Vec2d& offset)
{
    for (Trafo& trafo : trafos) {
        trafo.absolute_offset += offset;
    }
}

} // namespace

ArrangeInteractor::ArrangeInteractor(Scene::SceneInteractor& scene_interactor, const Workbench& workbench) :
    m_scene_interactor{scene_interactor},
    m_workbench{workbench}
{}

void ArrangeInteractor::apply_local_arrange_result(
    const SelectionId project_id,
    const std::vector<std::pair<BedRef, Pack>>& packs,
    const Packs& unpacked_packs,
    std::optional<std::size_t> config_container_to_add_beds
)
{
    for (auto [bed_ref, pack] : packs) {
        const BedInstance* bed_instance{
            m_workbench.project(project_id).find_bed_instance_by_id(bed_ref.instance_id)
        };
        if (!bed_instance) {
            continue;
        }
        Transformation bed_trafo{bed_instance->transformation};
        const Vec2d bed_offset{to_2d(bed_trafo.get_offset())};
        offset_trafos(pack.trafos, bed_offset);
        m_scene_interactor.transform_instances(pack.trafos);
    }

    if (config_container_to_add_beds) {
        for (Pack pack : unpacked_packs) {
            const BedInstance& bed_instance{
                m_scene_interactor.add_bed_instance(*config_container_to_add_beds)};

            Transformation bed_trafo{bed_instance.transformation};
            const Vec2d bed_offset{to_2d(bed_trafo.get_offset())};
            offset_trafos(pack.trafos, bed_offset);
            m_scene_interactor.transform_instances(pack.trafos);
        }
    } else {
        double offset{-20.0};
        for (Pack pack : unpacked_packs) {
            offset -= BB::sizes(pack.bounding_box).x();
            offset -= -5.0;
            offset_trafos(pack.trafos, Vec2d{offset, 0.0});
            m_scene_interactor.transform_instances(pack.trafos);
        }
    }

}

namespace {

struct PackingResult
{
    Packs packs;
    std::vector<ArrangeItem> unpacked;
};

std::optional<PackingResult> pack_to_bed(
    std::vector<ArrangeItem>& to_pack,
    const std::map<BedRef, std::vector<ArrangeItem>>& fixed_per_bed,
    const ArrangeBed arrange_bed,
    const StopCondition stop_condition,
    Settings settings,
    const std::vector<BedRef>& available_beds,
    const WipeTowerPerBed& wipe_tower_per_bed,
    std::size_t limit = 1000
)
{
    Packs packs;

    for (std::size_t index{}; index < limit; ++index) {
        ASSERT(index < 999, "Infinite arrange loop!");

        const std::optional<BedRef> bed_ref{
            index < available_beds.size() ? std::optional{available_beds[index]} : std::nullopt};

        std::optional<WipeTowerForArrange> wipe_tower;
        if (bed_ref) {
            const auto it{wipe_tower_per_bed.find(*bed_ref)};
            if (it != wipe_tower_per_bed.end()) {
                wipe_tower = it->second;
            }
        }

        const Points bed_contour{get_bed_contour(arrange_bed, settings)};

        std::vector<ArrangeItem> to_pack_with_tower{};
        std::vector<ArrangeItem> fixed_with_tower{};
        if (wipe_tower) {
            if (wipe_tower->is_fixed) {
                fixed_with_tower.push_back(wipe_tower->wipe_tower);
            } else {
                to_pack_with_tower.push_back(wipe_tower->wipe_tower);
                if (wipe_tower->wipe_tower.gravity_sink) {
                    settings.strategy = Arrange::Strategy::Gravity;
                }
            }
        }
        to_pack_with_tower.insert(to_pack_with_tower.end(), to_pack.begin(), to_pack.end());

        const auto it{bed_ref ? fixed_per_bed.find(*bed_ref) : fixed_per_bed.end()};

        bool fixed_on_bed{false};
        if (it != fixed_per_bed.end()) {
            const std::vector<ArrangeItem>& fixed{it->second};
            fixed_on_bed = !fixed.empty();
            fixed_with_tower.insert(fixed_with_tower.end(), fixed.begin(), fixed.end());
        }

        std::optional<ArrangeResult> result{Arrange::arrange(
            bed_contour,
            to_pack_with_tower,
            fixed_with_tower,
            settings,
            stop_condition
        )};
        if (!result) {
            return std::nullopt;
        }

        if (result->packed.empty() && !fixed_on_bed) {
            break;
        }

        Pack pack;
        pack.bounding_box = BB::unscaled<double>(BB::construct(bed_contour));
        for (ArrangeItem& item : result->packed) {
            const Vec2d item_offset{unscaled<double>(item.get_translation())};

            pack.trafos.push_back(
                Trafo{
                    .instance_ref    = item.get_element_ref(),
                    .absolute_offset = item_offset,
                    .rotation_delta  = item.get_rotation()
                }
            );
        }
        packs.push_back(std::move(pack));

        to_pack = result->not_packed;
    }
    PackingResult result{packs, to_pack};
    return result;
}

struct ArrangeLocalResult {
    std::vector<std::pair<BedRef, Pack>> packs;
    Packs unpacked_packs;
    std::vector<ArrangeItem> failed;
};

std::optional<ArrangeLocalResult> arrange_local(
    StopToken stop_token,
    ProgressTracker progress_tracker,
    const Settings settings,
    const ModelInstancesPerBed model_instances_per_bed,
    const std::vector<InstanceMeshes> extra_instances,
    const WipeTowerPerBed wipe_tower_per_bed
)
{
    const auto stop_condition{[=]() {
        return stop_token.stop_requested();
    }};

    std::vector<std::pair<BedRef, Pack>> packs;

    const std::optional<std::vector<InputShape>> extra_input{
        get_arrange_input(extra_instances, stop_condition)
    };
    if (!extra_input) {
        return std::nullopt;
    }

    std::set<ElementRef> extra_elements;
    std::vector<ArrangeItem> extra{to_arrange_items(*extra_input, settings)};
    for (const ArrangeItem& item : extra) {
        extra_elements.insert(item.get_element_ref());
    }
    std::vector<ArrangeItem> unpacked{};

    for (const BedWithInstances& bed_with_instances : model_instances_per_bed) {
        const std::optional<std::vector<InputShape>> arrangeable_input{
            get_arrange_input(bed_with_instances.arrangeable_instances, stop_condition)
        };
        if (!arrangeable_input) {
            return std::nullopt;
        }
        const std::optional<std::vector<InputShape>> fixed_input{
            get_arrange_input(bed_with_instances.fixed_instances, stop_condition)
        };
        if (!fixed_input) {
            return std::nullopt;
        }

        std::vector<ArrangeItem> arrangeable{to_arrange_items(*arrangeable_input, settings)};
        arrangeable.insert(arrangeable.end(), extra.begin(), extra.end());
        extra.clear();

        const std::map<BedRef, std::vector<ArrangeItem>> fixed{
            {bed_with_instances.bed_ref, to_arrange_items(*fixed_input, settings)}
        };

        if (auto packing_result{pack_to_bed(
                arrangeable,
                fixed,
                bed_with_instances.arrange_bed,
                stop_condition,
                settings,
                {bed_with_instances.bed_ref},
                wipe_tower_per_bed,
                1
            )})
        {
            if (!packing_result->packs.empty()) {
                ASSERT(packing_result->packs.size() == 1);
                packs.push_back({bed_with_instances.bed_ref, std::move(packing_result->packs.front())});
            }
            for (const auto& item : packing_result->unpacked) {
                if (extra_elements.contains(item.get_element_ref())) {
                    extra.push_back(item);
                } else {
                    unpacked.push_back(item);
                }
            }
        } else {
            return std::nullopt;
        }
    }

    unpacked.insert(unpacked.end(), extra.begin(), extra.end());

    Packs unpacked_packs;
    if (!model_instances_per_bed.empty()) {
        const BedWithInstances& bed_with_instances{model_instances_per_bed.front()};
        if (auto packing_result{pack_to_bed(
                unpacked,
                {},
                bed_with_instances.arrange_bed,
                stop_condition,
                settings,
                {bed_with_instances.bed_ref},
                {}
            )})
        {
            unpacked_packs = packing_result->packs;
            unpacked = packing_result->unpacked;
        } else {
            return std::nullopt;
        }
    }

    return ArrangeLocalResult{
        packs,
        unpacked_packs,
        unpacked
    };
}

ElementRefs get_not_arranged(const ArrangeLocalResult& result, bool add_unpacked_packs)
{
    ElementRefs not_arranged;
    for (const ArrangeItem& arrange_item : result.failed) {
        not_arranged.push_back(arrange_item.get_element_ref());
    }
    if (add_unpacked_packs) {
        for (const Pack& pack : result.unpacked_packs) {
            for (const Trafo& trafo : pack.trafos) {
                not_arranged.push_back(trafo.instance_ref);
            }
        }
    }
    return not_arranged;
}

} // namespace

void ArrangeInteractor::arrange(
    const Domain::SelectionId project_id,
    const std::vector<BedToArrange>& beds,
    std::optional<std::size_t> config_container_to_add_beds,
    const Domain::ConstModelInstanceList& extra,
    const Settings& settings,
    std::function<void()> on_finished,
    const std::string& job_name
)
{
    if (project_id == Domain::INVALID_ID) {
        if (on_finished) {
            on_finished();
        }
        return;
    }

    for (const ModelInstance* instance : extra) {
        if (!check_coord_bounds(to_2d(instance_bounding_box(*instance)))) {
            invoke_listeners<IArrangeEventsListener>(
                [&](auto* listener) { listener->on_fatal_arrange_error(project_id); });
            if (on_finished) {
                on_finished();
            }
            return;
        }
    }

    BedRefs bed_refs;
    std::set<BedRef> fixed_wipe_towers;
    for (const BedToArrange& bed_to_arrange : beds) {
        bed_refs.push_back(bed_to_arrange.ref);
        if (bed_to_arrange.fixed_wipe_tower) {
            fixed_wipe_towers.insert(bed_to_arrange.ref);
        }
    }

    const WipeTowerPerBed wipe_tower_per_bed{get_wipe_towers_per_bed(
        project_id,
        bed_refs,
        fixed_wipe_towers,
        settings,
        m_workbench,
        m_scene_interactor)};

    JobManager& job_manager{PlatformServices::instance().job_manager()};
    const ModelInstancesPerBed model_instances_per_bed{
        get_arrange_input_per_bed(project_id, beds, extra, settings, m_workbench)};

    job_manager
        .create_job(
            job_name,
            arrange_local,
            settings,
            model_instances_per_bed,
            get_meshes(extra, ResetTranslation::True),
            wipe_tower_per_bed)
        .set_project_id(project_id)
        .on_result(
            [this, project_id, settings, on_finished, config_container_to_add_beds](
                const std::optional<ArrangeLocalResult>& result)
            {
                if (!result || m_workbench.find_project_by_id(project_id) == nullptr) {
                    if (on_finished) {
                        on_finished();
                    }
                    return;
                }

                apply_local_arrange_result(project_id, result->packs, result->unpacked_packs, config_container_to_add_beds);

                if (on_finished) {
                    on_finished();
                }

                const ElementRefs not_arranged{get_not_arranged(*result, !config_container_to_add_beds)};
                if (!not_arranged.empty()) {
                    invoke_listeners<IArrangeEventsListener>(
                        [&](auto* listener)
                        { listener->on_elements_not_arranged(project_id, not_arranged); });
                }
            })
        .start();
}

void ArrangeInteractor::arrange_added_instances(
    const SelectionId project_id,
    const ElementRefs& added_instances,
    const BedRef& target_bed,
    const UndoSnapshotType snapshot_type)
{
    std::set<size_t> instance_ids;
    for (const ElementRef& ref : added_instances) {
        instance_ids.insert(ref.instance_id);
    }

    const bool is_queue_processing_running{!m_added_arrange_queue.empty()};
    m_added_arrange_queue.push({project_id, std::move(instance_ids), target_bed, snapshot_type});
    if (!is_queue_processing_running) {
        this->process_added_arrange_queue();
    }
}

void ArrangeInteractor::process_added_arrange_queue()
{
    const constexpr Settings ARRANGE_SETTINGS{
        .strategy        = Arrange::Strategy::Gravity,
        .scaled_offset   = Algorithms::Scaling::scaled(3.0),
        .allow_rotations = false
    };

    if (m_added_arrange_queue.empty()) {
        return;
    }

    const PendingArrange pending_arrange{m_added_arrange_queue.front()};
    const Project* project{m_workbench.find_project_by_id(pending_arrange.project_id)};

    if (!project) {
        m_added_arrange_queue.pop();
        process_added_arrange_queue();
        return;
    }

    const ConfigContainer* config_container{
        project->find_config_container(pending_arrange.target_bed.config_container_id)};

    const BedInstance* target_bed{
        project->find_bed_instance_by_id(pending_arrange.target_bed.instance_id)
    };

    if (!config_container || !target_bed) {
        m_added_arrange_queue.pop();
        process_added_arrange_queue();
        return;
    }

    std::vector<BedToArrange> beds;
    for (const auto& bed_instance : config_container->bed_instances()) {
        if (bed_instance->index() < target_bed->index()) {
            continue;
        }
        BedToArrange bed_to_arrange{
            .ref = {config_container->id().id, bed_instance->id().id},
            .index = bed_instance->index(),
            .fixed_wipe_tower = true
        };
        for (const ModelInstance* instnace : bed_instance->model_instances) {
            if (!pending_arrange.instance_ids.contains(ASSERT_VAL(instnace)->id().id)) {
                bed_to_arrange.fixed.push_back(instnace);
            }
        }
        beds.push_back(bed_to_arrange);
    }

    ConstModelInstanceList instances;
    for (const std::size_t instnace_id : pending_arrange.instance_ids) {
        const ModelInstance* instance{project->find_instance_by_id(instnace_id)};
        if (!instance) {
            continue;
        }
        instances.push_back(instance);
    }

    this->arrange(
        pending_arrange.project_id,
        beds,
        pending_arrange.target_bed.config_container_id,
        instances,
        ARRANGE_SETTINGS,
        [this, snapshot_type = pending_arrange.snapshot_type](){
            m_added_arrange_queue.pop();
            if (m_added_arrange_queue.empty()) {
                m_scene_interactor.undo_provider().take_snapshot(snapshot_type);
            } else {
                this->process_added_arrange_queue();
            }
        },
        "partial_arrange"
    );
}

} // namespace Slic3r::Biz
