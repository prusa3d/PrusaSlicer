#include "Slic3r/App/Plater/SimplifyGizmo.hpp"

#include <chrono>

#include "Slic3r/App/Plater/SimplifyNotification.hpp"
#include "Slic3r/App/Plater/SimplifyDialog.hpp"
#include "Slic3r/App/Plater/PlaterSceneLayer.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/App/Render/Geometry.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp" // geometry_from_triangle_mesh
#include "Slic3r/App/Render/Material.hpp"
#include "Slic3r/App/Scene/Node.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp" // JobManager + render_request_handler
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/Algorithms/QuadricEdgeCollapse.hpp"

#include "Slic3r/Domain/Color.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Types.hpp"

#include "Slic3r/Assert.hpp"

#include "fmt/format.h"

#include "imgui/imgui.h"

using namespace Slic3r;
using namespace Slic3r::Biz;

// TODO: Esc key down: cancel simplification
namespace Slic3r::App::Plater {
namespace { 
// Static variables
const float wireframe_width = 0.5f;
const Domain::ColorRGBA wireframe_color = Domain::ColorRGBA::BLUE();
const Domain::ColorRGBA model_color = Domain::ColorRGBA::WHITE();

const Domain::ModelVolume* get_volume_by_id(const Domain::ObjectID& volume_id, const Domain::Project& project) {
    for (const Domain::ModelObject* object : project.model().objects) {
        for (const Domain::ModelVolume* volume : object->volumes) {
            if (volume->id() == volume_id) {
                return volume;
            }
        }
    }
    return nullptr;
}

//Domain::ModelVolume* get_volume_by_id(const Domain::ObjectID& volume_id, Domain::Project& project) {
//    for (const Domain::ModelObject* object : project.model().objects) {
//        for (Domain::ModelVolume* volume : object->volumes) {
//            if (volume->id() == volume_id) {
//                return volume;
//            }
//        }
//    }
//    return nullptr;
//}

float detail_to_max_error(SimplifyLevelDetail detail)
{
    switch (detail) {
    using enum SimplifyLevelDetail;
    case ExtraHigh: return 1e-3f;
    case High:      return 1e-2f;
    case Medium:    return 0.1f;
    case Low:       return 0.5f;
    case ExtraLow:  return 1.f;
    default: return 0.1f; // should not appear
    }
}

Render::Material get_material(bool use_wireframe, Render::Device& device, const Scene::Scene& scene) {
    if (use_wireframe) {
        const Render::Rect& viewport = scene.camera().viewport();
        float half_w = 0.5f * float(viewport.width);
        float half_h = 0.5f * float(viewport.height);
        Domain::SquareMatrix4f viewport_matrix;
        viewport_matrix <<
            half_w, 0.0f, 0.0f, half_w,
            0.0f, half_h, 0.0f, half_h,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f;

        return Render::Material{}
            .set_shader(device.context().shader_manager().shader("gouraud_light_wireframe"))
            .set_uniform("uniform_color", model_color)
            .set_uniform("wireframe_color", wireframe_color)
            .set_uniform("wireframe_width", wireframe_width)
            .set_uniform("viewport_matrix", viewport_matrix)
            .set_transparent(false);
    } else {
        return Render::Material{}
            .set_shader(device.context().shader_manager().shader("gouraud_light"))
            .set_uniform("uniform_color", model_color)
            .set_transparent(false);
    }
}

// settings of the simplification
struct Configuration
{
    // switch between triangle_count and max_error
    // True  .. decimate until mesh triangle count is wanted_count
    // False .. remove triangle until smallest quadric error is greater than max_error
    bool use_count = false; 

    // Target count of triangles when use_count
    uint32_t wanted_count = 0;   // setted by decimation_ratio
    float max_error = 1.;        // maximal quadric error

    // Only for UI to show user percents
    float decimate_ratio = 50.f; // in percent

    // unified place to calculate wanted count
    void fix_count_by_ratio(size_t triangle_count);
};

using SelectedVolumeIds = std::set<Domain::ObjectID>;
using SimplifyData = std::map<Domain::ObjectID, std::unique_ptr<indexed_triangle_set> >;

struct Phantom {
    Domain::ObjectID volume_id;
    std::unique_ptr<Render::Geometry> geometry;
};
using Phantoms = std::vector<Phantom>;

struct SimplifyJobData {
    // Simplification modify data inplace
    SimplifyData data; // In / Out triangle meshes

    // Define termination for simplification
    Configuration configuration;

    // Identify source(s)
    SelectedVolumeIds volume_ids; // is same as data keys - store separate for compare
    // IMPROVE: remove variable volume_ids and
    //    compare ProjectContext::volume_ids -> std::set<Domain::ObjectID> 
    //       with std::views::keys(data)
    Domain::SelectionId project_id = Domain::INVALID_ID;
};

// Data dependent on the project
using Nodes = std::vector<Scene::Node*>;
struct ProjectContext {
    Configuration configuration;
    SelectedVolumeIds volume_ids; // current processing volumes

    size_t original_triangle_count = 0; // sum of the original triangle counts
    size_t triangle_count = 0; // phantom triangle count
    SimplifyLevelDetail detail = SimplifyLevelDetail::Medium; // current level of detail
    std::string mesh_name = "no name"; // showed name of the mesh (we are working on)
    Phantoms phantoms; // keep phantoms geometries
    Nodes phantom_nodes;
    
    bool show_wireframe = true; // flag that material is set to wireframe

    Nodes to_enable; // node to enable on close

    // NOTE: write by job but on the main thread
    int progress = 0; // percent of done work (100 == done)

    // result of simplification
    SimplifyJobData job_data;
    // edit only on the main thread
    bool is_job_running = false;
};

struct NodeInput {
    Domain::ObjectID volume_id = 0;
    const indexed_triangle_set* its = nullptr;
};
using NodeInputs = std::vector<NodeInput>;

bool exist_node(Scene::Node& root, const Scene::Node* node) {
    Scene::Node::NodeList volume_nodes;
    root.query([node](const Scene::Node* n) { return n == node; }, volume_nodes, true);
    return !volume_nodes.empty();
}
} // namespace

// use definition from anonym namespace
struct SimplifyGizmo::ProjectContext: public Plater::ProjectContext {};

SimplifyGizmo::SimplifyGizmo(
    Render::Device& device,
    PlaterScenePresenter& scene_presenter,
    Biz::ProjectInteractor& project_interactor,
    Scene::IGizmoController& gizmo_controller,
    std::unique_ptr<SimplifyNotification> notification
) :
    m_device(device),
    m_scene_presenter(scene_presenter),
    m_project_interactor(project_interactor),
    m_gizmo_controller(gizmo_controller),
    m_notification(std::move(notification)),
    m_proj_ctxs(std::make_unique<Biz::ProjectScoped<ProjectContext>>(project_interactor))
{
    m_dialog = std::make_unique<SimplifyDialog>();
    m_dialog->callbacks().close = [this]() { close(); };
    m_dialog->callbacks().use_count_changed = [this](bool use_count) {
        ProjectContext& proj_ctx = m_proj_ctxs->selected();
        if (use_count && proj_ctx.volume_ids.size() > 1)
            return; // not for multipart

        m_dialog->set_enabled_by_use_count(use_count);
        proj_ctx.configuration.use_count = use_count;

        if (use_count && proj_ctx.is_job_running == false)
            return; // already finished - no process
        
        process();        
    };

    m_dialog->callbacks().detail_level_changed = [this](SimplifyLevelDetail detail) {
        ProjectContext& proj_ctx = m_proj_ctxs->selected();
        if (proj_ctx.configuration.use_count)
            return; // editable only when not use_count
        proj_ctx.detail = detail;
        proj_ctx.configuration.max_error = detail_to_max_error(detail);
        process();
    };

    m_dialog->callbacks().decimate_ratio_changed = [this](double value) {
        ProjectContext& proj_ctx = m_proj_ctxs->selected();
        if (!proj_ctx.configuration.use_count)
            return; // editable only when use_count
        proj_ctx.configuration.decimate_ratio = value;
        proj_ctx.configuration.fix_count_by_ratio(proj_ctx.original_triangle_count);
        m_dialog->set_info_line(proj_ctx.configuration.wanted_count);
        process();        
    };

    m_dialog->callbacks().show_wireframe_checked = [this](bool checked) {
        ProjectContext& proj_ctx = m_proj_ctxs->selected();
        proj_ctx.show_wireframe = checked;

        Scene::Scene& scene = m_scene_presenter.scene();
        Render::Material material = get_material(proj_ctx.show_wireframe, m_device, scene);
        for (Scene::Node* node : proj_ctx.phantom_nodes) {
            if(!exist_node(scene.root(), node)) 
                continue;
            node->set_material_override(material);
        }

    };

    m_dialog->callbacks().apply = [this]() {
        apply_simplify();
    };
}

namespace {
std::string create_job_name(Domain::SelectionId project_id) {
    // Each project has its own rewritable job
    return fmt::format("SimplifyJob {}", project_id);
}

void stop_worker_thread_request(Domain::SelectionId project_id) {
    Biz::Platform::PlatformServices::instance()
        .job_manager().cancel_job(create_job_name(project_id));
}
} // namespace

SimplifyGizmo::~SimplifyGizmo() {
    const auto& projects = m_project_interactor.observable_project_list();
    for (size_t i = 0; i < projects.size(); i++)
    {
        Domain::SelectionId projcet_id = projects.at(i);
        stop_worker_thread_request(projcet_id);
    }
}

Scene::GizmoActivationState SimplifyGizmo::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
{    
    return Scene::GizmoActivationState::Inactive;
}

namespace {
void update_dialog(SimplifyDialog& dialog, const ProjectContext& proj_ctx) {

    // disable callback till new values are set
    SimplifyDialog::Callbacks temp_callbacks = std::move(dialog.callbacks());
    dialog.callbacks() = SimplifyDialog::Callbacks{};
    ScopeGuard sg_callbacks([&dialog, &temp_callbacks]() { dialog.callbacks() = std::move(temp_callbacks); });

    // update dialog data
    dialog.set_mesh_name(proj_ctx.mesh_name);
    dialog.set_triangles(proj_ctx.original_triangle_count);
    dialog.set_use_count(proj_ctx.configuration.use_count);
    dialog.set_detail_level(proj_ctx.detail);
    dialog.set_decimate_ratio_step(100. / proj_ctx.original_triangle_count);
    dialog.set_decimate_ratio(proj_ctx.configuration.decimate_ratio);
    dialog.set_info_line(proj_ctx.configuration.wanted_count);
    dialog.set_show_wireframe(proj_ctx.show_wireframe);
    ASSERT(!proj_ctx.volume_ids.empty());
    dialog.set_multipart(proj_ctx.volume_ids.size() != 1);
}
SelectedVolumeIds get_volume_ids(const Biz::Scene::ObjectSelection& selection, const Domain::Project& project)
{
    SelectedVolumeIds ids;
    for (const Domain::ElementRef& element : selection.elements) {
        if (element.volume_id == 0) { // is object
            const Domain::ModelObject* object = project.find_object_by_id(element.object_id);
            for (const Domain::ModelVolume* volume : object->volumes)
                ids.emplace(volume->id());
        }
        else { // volume selected
            ids.emplace(element.volume_id);
        }
    }
    return ids;
}

size_t is_one_object(const SelectedVolumeIds& volume_ids, const Domain::Project& project) {
    const Domain::ModelObject& object = 
        *get_volume_by_id(*volume_ids.begin(), project)->get_object();
    if (object.volumes.size() != volume_ids.size()) {
        return false; // not all volumes from object
    }
    const Domain::ObjectID& object_id = object.id();
        get_volume_by_id(*volume_ids.begin(), project)->get_object()->id();
    for (const Domain::ObjectID& volume_id : volume_ids) {
        if (get_volume_by_id(volume_id, project)->get_object()->id() != object_id)
            return false; // volume from different object
    }
    return true;
}

std::string create_mesh_name(const SelectedVolumeIds& volume_ids, const Domain::Project& project) {
    // NOTE: Need m_volume_ids to be set before calling this function.
    if (volume_ids.empty()) {
        assert(false); // should not appear
        return _u8L("Empty");
    } 
    if (volume_ids.size() == 1) { // volume name
        return get_volume_by_id(*volume_ids.begin(), project)->name;
    } 
    if (is_one_object(volume_ids, project)) { // object name
        return get_volume_by_id(*volume_ids.begin(), project)->get_object()->name;
    }

    // Create multi volume name shown in Simplification as name for simplified selection
    std::string name = fmt::format("{}[{}]:", _u8L("Multi-volume"), volume_ids.size());
    for (const Domain::ObjectID& volume_id : volume_ids) {
        const Domain::ModelVolume* volume_ptr = get_volume_by_id(volume_id, project);
        name += volume_ptr->get_object()->name + "-" + volume_ptr->name + ",";
    }
    return name;
}

NodeInputs create_node_inputs(const SelectedVolumeIds& volume_ids, const Domain::Project& project) {
    NodeInputs node_inputs;
    node_inputs.reserve(volume_ids.size());
    for (const Domain::ObjectID& volume_id : volume_ids) {
        // generate clone of goemetry (copy Node)
        const Domain::ModelVolume* volume_ptr = get_volume_by_id(volume_id, project);
        const indexed_triangle_set* its_ptr = &volume_ptr->mesh().its;
        node_inputs.push_back(NodeInput{ volume_id, its_ptr });
    }
    return node_inputs;
}

Nodes disable_nodes(const SelectedVolumeIds& volume_ids, Scene::Scene& scene) {
    Nodes to_enable;
    for (const Domain::ObjectID& volume_id : volume_ids) {
        Scene::Node::NodeList volume_nodes;
        scene.root().query([volume_id](const Scene::Node* n) -> bool {
            const Scene::SceneNodeTag* tag = n->tag_of_type<Scene::SceneNodeTag>();
            return tag != nullptr && tag->volume_id == volume_id.id;
            }, volume_nodes);
        ASSERT(!volume_nodes.empty());
        to_enable.reserve(to_enable.size() + volume_nodes.size());
        for (Scene::Node* volume_node : volume_nodes) {
            ASSERT(volume_node->enabled());
            volume_node->set_enabled(false); // make original volume invisible
            to_enable.push_back(volume_node); // save for later enable
        }
    }
    return to_enable;
}

void remove_simplify_nodes(Nodes& phantom_nodes, Scene::Scene& scene) {
    for (Scene::Node* node : phantom_nodes) {
        if (!exist_node(scene.root(), node)) {
            continue;
        }
        scene.remove_child(node);
    }
    phantom_nodes.clear();
}

void set_nodes(const NodeInputs& node_inputs, ProjectContext& proj_ctx, Render::Device& device, 
    Scene::Scene& scene, const PlaterScenePresenter::MeshManager& mesh_manager, const Domain::Project& project)
{
    proj_ctx.phantoms.clear();
    proj_ctx.phantoms.reserve(node_inputs.size());
    remove_simplify_nodes(proj_ctx.phantom_nodes, scene);

    const auto layer_index = Scene::RenderLayerId(PlaterSceneLayer::DocumentObjects);
    Render::Material material = get_material(proj_ctx.show_wireframe, device, scene);
    bool enable_ignored = true;
    for (const NodeInput& node_input: node_inputs) {
        const Domain::ObjectID& volume_id = node_input.volume_id;
        Scene::Node::NodeList volume_nodes;
        scene.root().query([volume_id](const Scene::Node* n) -> bool {
                const Scene::SceneNodeTag* tag = n->tag_of_type<Scene::SceneNodeTag>();
                return tag != nullptr && 
                    tag->volume_id == volume_id.id;
            }, volume_nodes, enable_ignored);
        ASSERT(!volume_nodes.empty());

        Scene::AuxiliaryElementId id{ Scene::AuxiliaryElementId::Type::Volume, volume_id.id };
        const Scene::TriangleMesh* mesh = mesh_manager.get(id);
        ASSERT(mesh != nullptr);
        const AABBMesh& aabb_mesh = mesh->aabb_mesh();

        // generate clone of goemetry (copy Node)
        const Domain::ModelVolume* volume_ptr = get_volume_by_id(volume_id, project);
        const Domain::Transform3d volume_tr = volume_ptr->get_matrix();
        Phantom phantom{ // keep geometry alive
            .volume_id = volume_id,
            .geometry = Render::geometry_from_triangle_mesh(device, *node_input.its)
        };
        proj_ctx.phantoms.emplace_back(std::move(phantom));
        const Render::Geometry* geometry = proj_ctx.phantoms.back().geometry.get();
        for (Scene::Node* volume_node : volume_nodes) {
            ASSERT(!volume_node->enabled()); // original MUST be invisible (disabled)
            Scene::NodeBuilder builder{scene};
            builder
                .set_debug_name("Simplified volume")
                .set_transform(volume_tr)
                .set_mesh(geometry, material, layer_index)
                .set_aabb(aabb_mesh);
            Scene::Node* phantom_node = builder.build().release();
            proj_ctx.phantom_nodes.push_back(phantom_node); // remember for removing
            scene.add_child(phantom_node, volume_node->parent());
        }
    }
}

} // namespace

void SimplifyGizmo::on_selection_change(Domain::SelectionId project_id, const Biz::Scene::ObjectSelection& selection) {
    bool is_current = project_id == m_project_interactor.selected_project_id();
    if (!enabled() || selection.elements.empty()) {
        if (is_current) close();
        return;
    }
    const Domain::Project& project = m_project_interactor.project(project_id);
    ProjectContext& proj_ctx = m_proj_ctxs->project(project_id);
    SelectedVolumeIds act_volume_ids = get_volume_ids(selection, project);
    if (act_volume_ids.empty()) { // selection do not contain simplifyable volumes
        if (is_current) close();
        return;
    }
    // Check selection of new volume (or change)
    // Do not reselect object when processing
    if (proj_ctx.volume_ids == act_volume_ids)
        return; // same selection
    proj_ctx.volume_ids = act_volume_ids;
    Scene::Scene& scene = m_scene_presenter.project_scene(project_id);
    proj_ctx.to_enable = disable_nodes(act_volume_ids, scene);
    proj_ctx.job_data = {}; // clear previous job data when exists
    NodeInputs node_inputs = create_node_inputs(act_volume_ids, project);
    const PlaterScenePresenter::MeshManager& mesh_manager = 
        m_scene_presenter.model_triangle_mesh_manager(project_id);
    set_nodes(node_inputs, proj_ctx, m_device, scene, mesh_manager, project);

    // sum up triangle count
    proj_ctx.original_triangle_count = 0;
    for (const NodeInput& node_input : node_inputs) {
        proj_ctx.original_triangle_count += node_input.its->indices.size(); 
    }

    proj_ctx.mesh_name = create_mesh_name(proj_ctx.volume_ids, project);
    proj_ctx.triangle_count = proj_ctx.original_triangle_count; // without decimation

    // Default value of configuration
    proj_ctx.configuration = Configuration{
        .use_count = false,
        .wanted_count = static_cast<uint32_t>(proj_ctx.original_triangle_count / 2),
        .max_error = detail_to_max_error(SimplifyLevelDetail::Medium),
        .decimate_ratio = 50.f
    };

    if (is_current) {
        update_dialog(dialog(), proj_ctx);

        // Start processing. If we switched from another object, process will
        // stop the background thread and it will restart itself later.
        process();
    }
}

namespace {
void add_listeners(
    Biz::Scene::SceneInteractor& scene_interactor,
    Scene::Scene& scene,
    SimplifyGizmo* gizmo
)
{
    scene_interactor.add_listener<Biz::Scene::ISceneSelectionChangedListener>(gizmo);
    scene_interactor.add_listener<Biz::Scene::ISceneChangedListener>(gizmo);
    scene.add_listener<Scene::IThumbnailRenderListener>(gizmo);
}

void remove_listeners(
    Biz::Scene::SceneInteractor& scene_interactor,
    Scene::Scene& scene,
    SimplifyGizmo* gizmo
)
{
    scene_interactor.remove_listener<Biz::Scene::ISceneSelectionChangedListener>(gizmo);
    scene_interactor.remove_listener<Biz::Scene::ISceneChangedListener>(gizmo);
    scene.remove_listener<Scene::IThumbnailRenderListener>(gizmo);
}
} // namespace

void SimplifyGizmo::on_activated()
{
    Biz::Scene::SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();
    Domain::SelectionId project_id                = m_project_interactor.selected_project_id();
    on_selection_change(project_id, scene_interactor.object_selection());
    add_listeners(scene_interactor, m_scene_presenter.scene(), this);
}

void SimplifyGizmo::on_deactivated()
{
    deactivate(m_project_interactor.selected_project_id());
    remove_listeners(m_project_interactor.scene_interactor(), m_scene_presenter.scene(), this);
}

void SimplifyGizmo::on_thumbnail_render_begin()
{
    // Before rendering a thumbnail, hide phantom nodes and show original model nodes.
    ProjectContext& proj_ctx = m_proj_ctxs->selected();
    Scene::Node& root_node   = m_scene_presenter.scene().root();
    for (Scene::Node* node : proj_ctx.phantom_nodes) {
        if (exist_node(root_node, node)) {
            node->set_enabled(false);
        }
    }

    for (Scene::Node* node : proj_ctx.to_enable) {
        if (exist_node(root_node, node)) {
            node->set_enabled(true);
        }
    }
}

void SimplifyGizmo::on_thumbnail_render_end()
{
    // After rendering the thumbnail, restore phantom nodes and hide original model nodes.
    ProjectContext& proj_ctx = m_proj_ctxs->selected();
    Scene::Node& root_node   = m_scene_presenter.scene().root();
    for (Scene::Node* node : proj_ctx.phantom_nodes) {
        if (exist_node(root_node, node)) {
            node->set_enabled(true);
        }
    }

    for (Scene::Node* node : proj_ctx.to_enable) {
        if (exist_node(root_node, node)) {
            node->set_enabled(false);
        }
    }
}

void SimplifyGizmo::on_project_activated(size_t new_project_id)
{
    // set dialog to current values
    const ProjectContext& proj_ctx = m_proj_ctxs->project(new_project_id);
    update_dialog(dialog(), proj_ctx);
    if (proj_ctx.is_job_running) {
        m_dialog->set_progress(proj_ctx.progress);
        m_dialog->set_enable_apply_button(false);
    } else {
        m_dialog->set_progress(100);
        m_dialog->set_enable_apply_button(!proj_ctx.job_data.data.empty());
    }

    add_listeners(m_project_interactor.scene_interactor(), m_scene_presenter.scene(), this);
}

void SimplifyGizmo::on_project_deactivated(size_t old_project_id)
{
    remove_listeners(m_project_interactor.scene_interactor(), m_scene_presenter.scene(), this);
}

Scene::ToolType SimplifyGizmo::type() const 
{ 
    return Scene::ToolType::Simplify; 
}

bool SimplifyGizmo::enabled() const
{
    const Biz::Scene::ObjectSelection& selection{
        m_project_interactor.scene_interactor().object_selection()
    };
    return !selection.empty() && !selection.contains_wipe_tower();
}

GizmoWindowPtr SimplifyGizmo::release_ui_window()
{
    return m_dialog.release();
}

void SimplifyGizmo::on_scene_selection_changed(Domain::SelectionId project_id, const Biz::Scene::ObjectSelection& selection)
{
    deactivate(project_id);
    on_selection_change(project_id, selection);
}

namespace {
NodeInputs into_node_inputs(const SimplifyData& data) {
    NodeInputs node_inputs;
    node_inputs.reserve(data.size());
    for (const auto& item : data) {
        const Domain::ObjectID& volume_id = item.first;
        const indexed_triangle_set& its = *item.second;
        node_inputs.push_back(NodeInput{ volume_id, &its });
    }
    return node_inputs;
}

void recreate_simplify_nodes(Domain::SelectionId project_id,
    ProjectContext& proj_ctx,
    Render::Device& device,
    PlaterScenePresenter& scene_presenter,
    const Biz::ProjectInteractor& project_interactor) 
{
    Scene::Scene& scene = scene_presenter.project_scene(project_id);
    const Domain::Project& project = project_interactor.project(project_id);    
    NodeInputs node_inputs = (proj_ctx.job_data.data.empty())?
        create_node_inputs(proj_ctx.volume_ids, project) :
        into_node_inputs(proj_ctx.job_data.data);
    const PlaterScenePresenter::MeshManager& mesh_manager =
        scene_presenter.model_triangle_mesh_manager(project_id);
    set_nodes(node_inputs, proj_ctx, device, scene, mesh_manager, project);
}

} // namespace

void SimplifyGizmo::on_volume_transformed(Domain::SelectionId project_id, const Domain::ElementRefs& elements,
    Biz::Scene::TransformState state, const Biz::BedTrackingChanges& bed_tracking_changes)
{
    ProjectContext& proj_ctx = m_proj_ctxs->project(project_id);
    recreate_simplify_nodes(project_id, proj_ctx, m_device, m_scene_presenter, m_project_interactor);
}

void SimplifyGizmo::on_instance_transformed(Domain::SelectionId project_id, const Domain::ElementRefs& elements,
    Biz::Scene::TransformState state, const Biz::BedTrackingChanges& bed_tracking_changes)
{
    ProjectContext& proj_ctx = m_proj_ctxs->project(project_id);
    recreate_simplify_nodes(project_id, proj_ctx, m_device, m_scene_presenter, m_project_interactor);
}

void SimplifyGizmo::deactivate(Domain::SelectionId project_id) {
    stop_worker_thread_request(project_id);
    ProjectContext& proj_ctx = m_proj_ctxs->project(project_id);
    proj_ctx.is_job_running = false; // for sure that thread did not finish without setting flag

    Scene::Scene& scene = m_scene_presenter.project_scene(project_id);
    // Enable previously disabled node
    for (Scene::Node* node : proj_ctx.to_enable) {// TODO: iterate over existing and enable only when exist
        if (!exist_node(scene.root(), node))
            continue;
        node->set_enabled(true); // make original volume visible again
    }
    proj_ctx.to_enable.clear();
    remove_simplify_nodes(proj_ctx.phantom_nodes, scene);

    // Free geometries
    proj_ctx.phantoms.clear();
    proj_ctx.volume_ids.clear();
    proj_ctx.job_data = {};
}

void SimplifyGizmo::close(){ m_gizmo_controller.deactivate_current_tool(); }
void SimplifyGizmo::apply_simplify()
{
    ProjectContext& proj_ctx = m_proj_ctxs->selected();
    proj_ctx.to_enable.clear();

    // check that there is NO change of volume
    assert(proj_ctx.job_data.volume_ids == proj_ctx.volume_ids);
    const Domain::Project& project = m_project_interactor.selected_project();
    const Domain::ElementRefs& elements = m_project_interactor.scene_interactor().object_selection().elements;
    auto get_selected_instance_id = [&elements](const Domain::ModelVolume& volume) -> size_t{
        size_t object_id = volume.get_object()->id().id;
        size_t volume_id = volume.id().id;
        for (const Domain::ElementRef& element : elements) {
            if (element.object_id == object_id &&
                element.volume_id == volume_id)
                return element.instance_id; // keep selected instance still selected
        }
        // current selection do not contain simplified volume yet, just use first instance id
        return volume.get_object()->instances.front()->id().id;
    };

    Biz::Scene::SceneInteractor::RefMeshes meshes;
    SimplifyData& result = proj_ctx.job_data.data;
    meshes.reserve(result.size());
    for (const auto& item : result) {
        const Domain::ObjectID& volume_id = item.first;
        indexed_triangle_set& its = *item.second;

        const Domain::ModelVolume* volume = get_volume_by_id(volume_id ,project);
        if (volume == nullptr)
            continue;
        
        size_t object_id = volume->get_object()->id().id;
        Domain::ElementRef ref(object_id, get_selected_instance_id(*volume), volume_id.id);

        using Biz::Algorithms::TriangleMesh::construct;
        meshes.emplace_back(ref, construct(std::move(its)));
    }
    result.clear();

    // Check if any volume was painted before mesh change clears it.
    const bool was_painted = std::ranges::any_of(
        meshes,
        [&project](const Biz::Scene::SceneInteractor::RefMesh& mesh)
        {
            const Domain::ModelVolume* volume = get_volume_by_id(mesh.first.volume_id, project);
            return volume && volume->is_painted();
        }
    );

    close(); // unregistr on_selection_change

    Domain::ElementRefs refs;
    refs.reserve(meshes.size());
    std::ranges::transform(meshes, std::back_inserter(refs), [](const auto& m) { return m.first; });
    m_notification->on_simplify(m_project_interactor.selected_project_id(), refs);

    Biz::Scene::SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();
    scene_interactor.change_volume_meshes(std::move(meshes));

    if (was_painted) {
        m_notification->on_paint_removed_after_simplify();
    }
}

namespace {
// Run on main thread
void finalize_job(SimplifyJobData&& result, SimplifyGizmo::ProjectContexts& proj_ctxs, 
    const Biz::ProjectInteractor& project_interactor, PlaterScenePresenter& scene_presenter, 
    Render::Device& device, SimplifyDialog& dialog) 
{
    if (!project_interactor.project_exists(result.project_id))
        return; // Project doesnt exist

    ProjectContext& proj_ctx = proj_ctxs.project(result.project_id);
    proj_ctx.is_job_running = false;

    if (proj_ctx.volume_ids.empty())
        return; // SimplifyGizmo is already closed, do not apply result

    if (proj_ctx.volume_ids != result.volume_ids)
        return; // Result is for previous selection NOT current 

    if (result.data.empty())
        return; // No data to finalize

    Configuration& cfg = proj_ctx.configuration;
    cfg = result.configuration; // update curren configuration

    bool use_ratio = !cfg.use_count &&
        proj_ctx.volume_ids == result.volume_ids;

    // calculate wanted count and fix current decimation ration
    if (use_ratio) {
        // calculate result triangle count
        proj_ctx.triangle_count = 0;
        for (const auto& [_, its_ptr] : result.data)
            proj_ctx.triangle_count += its_ptr->indices.size();
        
        // set wanted count to current result
        cfg.wanted_count = proj_ctx.triangle_count;
        // calculate decimation ration for result triangle count
        cfg.decimate_ratio = 100.f
            * (1.0f - (cfg.wanted_count / (float)proj_ctx.original_triangle_count));
    }
    Domain::SelectionId project_id = result.project_id;
    proj_ctx.job_data = std::move(result);

    recreate_simplify_nodes(project_id, proj_ctx, device, scene_presenter, project_interactor);

    if (project_id == project_interactor.selected_project_id()) {
        // rerender the UI to show result.
        dialog.set_progress(100.);
        dialog.set_enable_apply_button(true);

        if (use_ratio) {
            dialog.set_info_line(cfg.wanted_count);
            dialog.set_decimate_ratio(cfg.decimate_ratio);        
        }
        Biz::Platform::PlatformServices::instance()
            .render_request_handler().request_render();
    }
}

// Create a copy of current meshes to pass to the worker thread.
// Using unique_ptr instead of pass-by-value to avoid an extra
// copy (which would happen when passing to std::thread).
SimplifyData create_simplify_data(const Domain::Project& project, const SelectedVolumeIds& volume_ids) {
    SimplifyData simplify_data;    
    for (const Domain::ObjectID& volume_id : volume_ids) {
        const Domain::ModelVolume* volume = get_volume_by_id(volume_id, project);
        simplify_data[volume_id] = std::make_unique<indexed_triangle_set>(volume->mesh().its); // copy
    }
    return simplify_data;
}

// Copy current configuration that will be used in job.
SimplifyJobData create_job_data(const ProjectContext& proj_ctx, const Biz::ProjectInteractor& project_interactor) {
    const Domain::Project& project = project_interactor.selected_project();
    return SimplifyJobData {
        .data = create_simplify_data(project, proj_ctx.volume_ids),
        .configuration = proj_ctx.configuration, // copy current configuration
        .volume_ids = proj_ctx.volume_ids, // copy current input definition
        .project_id = project_interactor.selected_project_id() // current project
    };
}

void set_progress(int percent, Domain::SelectionId project_id, 
    SimplifyGizmo::ProjectContexts& proj_ctxs,
    const Biz::ProjectInteractor& project_interactor,
    SimplifyDialog& dialog ) 
{
    if (!project_interactor.project_exists(project_id))
        return;
    ProjectContext& proj_ctx = proj_ctxs.project(project_id);
    if (!proj_ctx.is_job_running)
        return; // already finished

    proj_ctx.progress = percent;
    if (project_interactor.selected_project_id() == project_id) {
        dialog.set_progress(percent);
        // Redraw the UI to show progress bar.
        Biz::Platform::PlatformServices::instance()
            .render_request_handler().request_render();
    }
}

std::function<void(int)> create_status_fn(Domain::SelectionId project_id,
    SimplifyGizmo::ProjectContexts& proj_ctxs,
    const Biz::ProjectInteractor& project_interactor,
    SimplifyDialog& dialog) {
    // Called by worker thread, updates progress bar.
    // Using CallAfter so the rerequest function is run in UI thread.
    // This is only the way to solved lock for previously removed project
    return [project_id, &proj_ctxs, &project_interactor, &dialog](int percent) {
        Biz::Platform::PlatformServices::instance()
            .main_thread_dispatcher().dispatch_on_main_thread(
            [percent, project_id, &proj_ctxs, &project_interactor, &dialog]() {
                set_progress(percent, project_id, proj_ctxs, project_interactor, dialog);
            });
        };
}
} // namespace

void SimplifyGizmo::process()
{
    ProjectContext& proj_ctx = m_proj_ctxs->selected();
    assert(!proj_ctx.volume_ids.empty());
    if (proj_ctx.volume_ids.empty())
        return; // no volume to process    

    if (!proj_ctx.is_job_running && 
        !proj_ctx.job_data.data.empty() &&
        proj_ctx.configuration.use_count &&
        proj_ctx.configuration.wanted_count == proj_ctx.triangle_count) {
        return; // correct number of triangles
    }

    // invalidate option to apply result
    m_dialog->set_enable_apply_button(false);
    
    using Biz::JThread::StopToken;
    std::function<SimplifyJobData(StopToken, SimplifyJobData&&)> process = 
        [this](StopToken stop_token, SimplifyJobData&& job_data)
    {
        // Checks that the UI thread did not request cancellation, throws if so.
        std::function<bool(void)> is_stopped = [&stop_token]() {
            return stop_token.stop_requested(); };

        std::function<void(int)> statusfn = 
            create_status_fn(job_data.project_id, *m_proj_ctxs, m_project_interactor, dialog());
        statusfn(0); // initialize percentage

        // Initialize.
        const Configuration& cfg = job_data.configuration;
        uint32_t triangle_count = cfg.use_count ? cfg.wanted_count : 0;
        float max_error = (!cfg.use_count) ? cfg.max_error : std::numeric_limits<float>::max();

        // Start the actual calculation.
        for (const auto& [_, triangles]: job_data.data) {
            float me = max_error;
            Biz::Algorithms::its_quadric_edge_collapse(
                *triangles, triangle_count, &me, is_stopped, statusfn);
        }       
        return job_data; // move
    };

    std::function<void(SimplifyJobData&&)> finalize = [this](SimplifyJobData&& result) {
        finalize_job(std::move(result), *m_proj_ctxs, m_project_interactor, 
            m_scene_presenter, m_device, dialog());
    };

    const Domain::SelectionId project_id = m_project_interactor.selected_project_id();
    std::string job_name = create_job_name(project_id);
    proj_ctx.is_job_running = true;
    Biz::Platform::PlatformServices::instance()
        .job_manager()
        .create_job(job_name, process, create_job_data(proj_ctx, m_project_interactor))
        .set_project_id(project_id)
        .on_result(finalize)
        .start();
}

/////////////////
/// namespace::Configuration
///////////////// 
namespace{
void Configuration::fix_count_by_ratio(size_t triangle_count)
{
    if (decimate_ratio <= 0.f)
        wanted_count = static_cast<uint32_t>(triangle_count);
    else if (decimate_ratio >= 100.f)
        wanted_count = 0;
    else
        wanted_count = static_cast<uint32_t>(
            std::round(triangle_count * (100.f - decimate_ratio) / 100.f)
        );
}

} // namespace
} // namespace Slic3r::App::Plater
