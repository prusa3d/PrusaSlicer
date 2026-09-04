
#include "Slic3r/App/Plater/SvgGizmo.hpp"
#include <Slic3r/App/Plater/SvgDialog.hpp>
#include <Slic3r/App/Render/Types.hpp>
#include <Slic3r/App/Scene/SceneNodeTag.hpp>
#include <Slic3r/App/Scene/Ray.hpp>
#include <Slic3r/App/Scene/EmbossCreate.hpp>

// file open dialog
#include <Slic3r/App/AppServices.hpp>
#include <Slic3r/App/AppConfig.hpp> // last open project directory
#include <Slic3r/App/IDialogManager.hpp>
#include <Slic3r/App/Wildcards.hpp>

#include <Slic3r/Assert.hpp>
#include <Slic3r/Domain/Point.hpp>
#include <Slic3r/Domain/ModelObject.hpp> // add volume into object
#include <Slic3r/Biz/I18N/I18N.hpp> // translations
#include <Slic3r/Biz/Emboss/EmbossJob.hpp> // embossing jobs
#include <Slic3r/Biz/Emboss/SvgShapeProvider.hpp>
#include <Slic3r/Biz/Emboss/NSVGUtils.hpp>
#include <Slic3r/Biz/Algorithms/BoundingBox.hpp>
#include <Slic3r/Biz/Algorithms/ExPolygonsWithId.hpp>
#include <Slic3r/Biz/Algorithms/Point.hpp>
#include "Slic3r/Biz/IMessageDialogProvider.hpp"

#include <boost/nowide/fstream.hpp> // Save SVG file
#include <fmt/format.h>

using Slic3r::Biz::_u8L;

namespace {
using namespace Slic3r;
using Slic3r::Biz::Emboss::Scale;
using Slic3r::Biz::Emboss::SvgShapeProvider;

struct ProjectContext
{
    std::string warning_tooltip; // Issue list shows on hover

    bool is_size_locked = true; // keep aspect ratio
    Domain::BoundingBox2d shape_bb; // size from svg file in mm

    std::optional<float> from_surface; // [in mm]

    // when no surface point and change 'from surface', relative move is made
    bool exist_surface_point;

    std::optional<float> up_limit = Biz::Emboss::UP_LIMIT;
    std::optional<float> rotation; // [in radians]

    Scale volume_scale; // setted in function calc_scale()

    // Is used to edit emboss and send changes to job
    // Inside volume is current state of shape WRT Volume
    Domain::EmbossShape shape; // copy from m_volume for edit
    // Contain EmbossProjection (with depth and use_surface)
    // Contain SvgFile (filepath)

    bool use_inch = false; // otherwise milimeters
    bool use_deg  = true; // otherwise radians

    Domain::ObjectID last_loaded_volume_id; // initial invalid
};
} // namespace

namespace Slic3r::App::Plater {

struct SvgGizmo::ProjectContext : public ::ProjectContext
{};

SvgGizmo::SvgGizmo(
    PlaterScenePresenter& scene_presenter,
    Biz::ProjectInteractor& project_interactor,
    Scene::IGizmoController& gizmo_controller
) :
    m_scene_presenter(scene_presenter),
    m_project_interactor(project_interactor),
    m_gizmo_controller(gizmo_controller),
    m_surface_drag(scene_presenter, project_interactor),
    m_proj_ctxs(std::make_unique<Biz::ProjectScoped<ProjectContext>>(project_interactor))
{}

SvgGizmo::~SvgGizmo() = default;

namespace {
void set_dialog_surface_distance(SvgDialog& dialog, const ProjectContext& proj_ctx);
bool calc_scales(Scale& volume_scale, const Domain::ModelVolume& volume);
const Domain::ModelVolume* get_selected_svg_volume(
    const Biz::ProjectInteractor& project_interactor
);
Domain::ModelVolume& get_selected_volume(Biz::ProjectInteractor& project_interactor);
std::string get_filename(const Domain::EmbossShape::SvgFile& svg);
Domain::Vec2d get_world_size(const ProjectContext& proj_ctx); // current (world) size in mm
void calc_from_surface(
    ProjectContext& proj_ctx,
    const Domain::Project& project,
    const Domain::ElementRef& ref,
    PlaterScenePresenter& scene_presenter
);
} // namespace

GizmoWindowPtr SvgGizmo::release_ui_window()
{
    auto dialog_ptr = std::make_unique<SvgDialog>();
    // Keep finger crossed that dialog will be alive as long as gizmo,
    // Need refactor from author of Yoga::Passthrough
    m_dialog                = dialog_ptr.get(); // keep connected to dialog
    auto& callbacks         = dialog_ptr->callbacks();
    callbacks.depth_changed = [this](double value)
    {
        ProjectContext& proj_ctx = m_proj_ctxs->selected();
        if (value <= SvgDialog::MIN_DEPTH) {
            value = SvgDialog::MIN_DEPTH;
            if (Domain::is_approx(proj_ctx.shape.projection.depth, value))
                return; // do not update already min value
        }
        proj_ctx.shape.projection.depth = value / proj_ctx.volume_scale.depth.value_or(1.);
        // change from surface limits
        set_dialog_surface_distance(dialog(), proj_ctx);
        update_volume();
    };
    callbacks.size_changed = [this](const Domain::Vec2d& size)
    {
        ProjectContext& proj_ctx      = m_proj_ctxs->selected();
        Domain::Vec2d want_size       = get_world_size(proj_ctx);
        const Domain::Vec2d prev_size = want_size; // copy
        if (!Domain::is_approx(0., size.x())) {
            want_size.x() = size.x();
        } else if (proj_ctx.is_size_locked) {
            want_size.x() *= size.y() / prev_size.y();
        }
        if (!Domain::is_approx(0., size.y())) {
            want_size.y() = size.y();
        } else if (proj_ctx.is_size_locked) {
            want_size.y() *= size.x() / prev_size.x();
        }
        // Relative scale in axis x and y
        double sx = want_size.x() / prev_size.x();
        double sy = want_size.y() / prev_size.y();
        Domain::Transform3d relative_volume_tr{Eigen::Scaling<double>(sx, sy, 1.)};
        Biz::Emboss::transform_selection_relative(relative_volume_tr, m_project_interactor);
        // NOTE: need before voulume update -> tesselation
        calc_scales(proj_ctx.volume_scale, *get_selected_svg_volume(m_project_interactor));
        proj_ctx.shape.shapes_with_ids = {}; // force to recalculate shapes on thread
        proj_ctx.shape.final_shape     = {};

        // Can change tesselation - update shape
        update_volume();
    };
    callbacks.unlock_size = [this](bool unlocked)
    { m_proj_ctxs->selected().is_size_locked = !unlocked; };
    callbacks.use_surface_checked = [this](bool checked)
    {
        m_proj_ctxs->selected().shape.projection.use_surface = checked;
        update_volume();
    };
    callbacks.surface_distance_changed = [this](double distance_in_mm)
    {
        ProjectContext& proj_ctx = m_proj_ctxs->selected();
        if (!proj_ctx.exist_surface_point)
            return; // disallow to change distance when no surface point
        std::optional<float>& distance = proj_ctx.from_surface;
        double diff                    = distance_in_mm - distance.value_or(0.f);
        if (Domain::is_approx(diff, 0., 1e-3))
            return; // no change

        if (const std::optional<double>& scale = proj_ctx.volume_scale.depth; scale.has_value())
            diff = diff / (*scale);

        Domain::Transform3d relative_volume_tr{Eigen::Translation3d(Domain::Vec3d(0., 0., diff))};
        Biz::Emboss::transform_selection_relative(relative_volume_tr, m_project_interactor);

        // calculate current surface distance
        const Domain::Project& project = m_project_interactor.selected_project();
        const Domain::ElementRef& ref =
            m_project_interactor.scene_interactor().object_selection().elements.front();
        calc_from_surface(proj_ctx, project, ref, m_scene_presenter);
    };
    callbacks.rotation_changed = [this](double angle_in_rad)
    {
        ProjectContext& proj_ctx      = m_proj_ctxs->selected();
        std::optional<float>& current = proj_ctx.rotation;
        float diff                    = static_cast<float>(angle_in_rad) - current.value_or(0.f);
        if (Domain::is_approx(diff, 0.f, 1e-3f))
            return; // approx same

        Domain::Transform3d relative_volume_tr{Eigen::AngleAxisd(diff, Domain::Vec3d::UnitZ())};
        Biz::Emboss::transform_selection_relative(relative_volume_tr, m_project_interactor);

        // recalculate current rotation
        const Domain::Project& project = m_project_interactor.selected_project();
        const Domain::ElementRef& el =
            m_project_interactor.scene_interactor().object_selection().elements.front();
        current = Biz::Emboss::calc_rotation(project, el);

        // update shape when needed
        if (proj_ctx.shape.projection.use_surface)
            update_volume();
    };
    callbacks.unlock_rotation = [this](bool unlocked)
    {
        auto& up_limit = m_proj_ctxs->selected().up_limit;
        if (unlocked) {
            up_limit.reset();
        } else { // Limit direction of the up vector on the model, between side and top surface
            up_limit = Biz::Emboss::UP_LIMIT;
        }
    };

    auto mirror = [this](bool is_x_otherwise_y)
    {
        double sx = is_x_otherwise_y ? -1. : 1.;
        double sy = -sx;
        Domain::Transform3d relative_volume_tr{Eigen::Scaling<double>(sx, sy, 1.)};
        Biz::Emboss::transform_selection_relative(relative_volume_tr, m_project_interactor);

        ProjectContext& proj_ctx = m_proj_ctxs->selected();
        // if (proj_ctx.up_limit.has_value())
        // proj_ctx.rotation = Biz::Emboss::calc_rotation(project, ref);
        if (proj_ctx.shape.projection.use_surface)
            update_volume();
    };
    callbacks.mirror_x        = [mirror]() { mirror(true); };
    callbacks.mirror_y        = [mirror]() { mirror(false); };
    callbacks.face_the_camera = [this]()
    {
        const Scene::Camera& camera = m_scene_presenter.scene().camera();
        Domain::Vec3d wanted_dir    = -camera.forward();

        const Domain::Project& project                = m_project_interactor.selected_project();
        Biz::Scene::SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();
        const Domain::ElementRef& ref = scene_interactor.object_selection().elements.front();

        const Domain::ModelInstance& instance =
            *project.find_instance_by_id(ref.object_id, ref.instance_id);
        const Domain::ModelVolume& volume         = (ref.volume_id != 0) ?
            *project.find_volume_by_id(ref.object_id, ref.volume_id) :
            *project.find_object_by_id(ref.object_id)->volumes.front();
        Domain::Transform3d to_world              = instance.get_matrix() * volume.get_matrix();
        const Domain::Transform3d& instance_tr    = instance.get_matrix();
        const Domain::Transform3d instance_tr_inv = instance_tr.inverse();
        const Domain::Transform3d& volume_tr_inv  = volume.get_matrix().inverse();

        Domain::Vec3d world_position      = to_world.translation();
        ProjectContext& proj_ctx          = m_proj_ctxs->selected();
        const auto& up_limit              = proj_ctx.up_limit;
        Domain::Transform3d new_volume_tr = Biz::Emboss::get_volume_transformation(
            to_world,
            wanted_dir,
            world_position,
            instance_tr_inv,
            proj_ctx.rotation,
            proj_ctx.from_surface,
            up_limit
        );
        Domain::Transform3d volume_relative =
            instance_tr * new_volume_tr * volume_tr_inv * instance_tr_inv;
        scene_interactor.transform_selection(volume_relative.matrix());

        if (!up_limit.has_value()) { // recalculate angle when not locked
            proj_ctx.rotation = Biz::Emboss::calc_rotation(project, ref);
            dialog().set_rotation(proj_ctx.rotation.value_or(0.f));
        }

        // update shape when needed
        if (proj_ctx.shape.projection.use_surface)
            update_volume();
    };

    auto set_svg_filepath = [this](const std::string& file_path)
    {
        ProjectContext& proj_ctx   = m_proj_ctxs->selected();
        Domain::EmbossShape& shape = proj_ctx.shape;
        shape.svg_file->path       = file_path;
        shape.svg_file->file_data  = {}; // force to reload file data
        const Scale& scale         = proj_ctx.volume_scale;
        Biz::Emboss::ReadShapeResult res =
            Biz::Emboss::read_shape_from_file(shape, scale.width, scale.height);
        if (res != Biz::Emboss::ReadShapeResult::success) {
            // revert changes of shape from volume
            shape = *get_selected_volume(m_project_interactor).emboss_shape; // copy back
            // show message box with error
            Biz::IMessageDialogProvider& dialog_provider = AppServices::instance().dialog_manager();
            dialog_provider.show_error_dialog(
                Biz::Emboss::to_string(res, file_path),
                _u8L("Error loading SVG file")
            );
            return;
        }
        update_volume();
    };
    callbacks.reload_file = [this, set_svg_filepath]()
    {
        ProjectContext& proj_ctx     = m_proj_ctxs->selected();
        const std::string& file_path = proj_ctx.shape.svg_file->path;
        if (file_path.empty())
            return; // button should be disabled
        set_svg_filepath(file_path);
    };
    callbacks.change_file = [this, set_svg_filepath]()
    {
        IDialogManager::FileCallback open_svg_callback =
            [set_svg_filepath](bool success, const std::vector<boost::filesystem::path>& file_paths)
        {
            if (!success || file_paths.size() < 1)
                return;
            std::string file_path = file_paths.front().string();
            set_svg_filepath(file_path);
        };

        auto& dlg_manager = App::AppServices::instance().dialog_manager();
        dlg_manager.show_file_dialog(
            FileDialogType::Open,
            _u8L("Change '.svg' file"),
            m_project_interactor.project_dir(
                m_project_interactor.selected_project_id(),
                AppServices::instance().app_config().get<std::string>("last_used_directory")
            ),
            "",
            Wildcards::generate_wildcards(Wildcards::TypeFlag::Svg),
            open_svg_callback
        );
    };
    callbacks.forgot_filepath = [this]()
    {
        Domain::EmbossShape::SvgFile& svg_file = *m_proj_ctxs->selected().shape.svg_file;
        svg_file.path.clear();
        dialog().set_shape(m_proj_ctxs->selected().shape);
        dialog().set_enable_reload_from_disk(false);
        // update volume
        get_selected_volume(m_project_interactor).emboss_shape->svg_file->path.clear();
    };
    callbacks.bake = [this]()
    {
        get_selected_volume(m_project_interactor).emboss_shape.reset();
        close();
    };
    callbacks.save_as = [this]()
    {
        IDialogManager::FileCallback save_svg_callback =
            [this](bool success, const std::vector<boost::filesystem::path>& file_paths)
        {
            if (!success || file_paths.size() < 1)
                return;
            std::string file_path = file_paths.front().string();
            boost::nowide::ofstream stream(file_path);
            if (!stream.is_open())
                return; // could not open file

            ProjectContext& proj_ctx = m_proj_ctxs->selected();
            stream << *proj_ctx.shape.svg_file->file_data;

            // change source file
            Domain::ModelVolume& volume            = get_selected_volume(m_project_interactor);
            Domain::EmbossShape::SvgFile& svg_file = *volume.emboss_shape->svg_file;
            svg_file.path                          = file_path; // forget old path
            svg_file.path_in_3mf.clear(); // possible change name
            proj_ctx.shape.svg_file = svg_file; // copy
            dialog().set_shape(*volume.emboss_shape);
        };

        auto& dlg_manager = App::AppServices::instance().dialog_manager();
        dlg_manager.show_file_dialog(
            FileDialogType::Save,
            _u8L("Save as '.svg' file"),
            m_project_interactor.project_dir(
                m_project_interactor.selected_project_id(),
                AppServices::instance().app_config().get<std::string>("last_used_directory")
            ),
            "",
            Wildcards::generate_wildcards(Wildcards::TypeFlag::Svg),
            save_svg_callback
        );
    };
    callbacks.operation_selection_changed = [this](Domain::ModelVolumeType type)
    { update_volume(type); };
    return dialog_ptr;
}

Scene::GizmoActivationState SvgGizmo::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
{
    return Scene::GizmoActivationState::Inactive;
}

bool SvgGizmo::on_drag_start(const Scene::GizmoEventContext& ctx)
{
    return m_surface_drag.on_drag_start(ctx, m_proj_ctxs->selected().from_surface);
}

namespace {
const Domain::ModelVolume*
get_selected_svg_volume(const Domain::Project& project, const Domain::ElementRefs& elements);
void update_svg_dialog(
    SvgDialog& dialog,
    const ProjectContext& proj_ctx,
    const Domain::ModelVolume& volume
);
} // namespace

bool SvgGizmo::on_dragging(const Scene::GizmoEventContext& ctx)
{
    ProjectContext& proj_ctx = m_proj_ctxs->selected(); // Can recalculate rotation during drag
    const auto& up_limit     = proj_ctx.up_limit;
    if (!m_surface_drag.on_dragging(ctx, proj_ctx.rotation, proj_ctx.from_surface, up_limit))
        return false;

    if (!m_surface_drag.is_dragging())
        return true; // out of surface but still dragging

    if (!up_limit.has_value()) { // recalculate angle when not locked
        const Domain::Project& project = m_project_interactor.selected_project();
        const Domain::ElementRef& element =
            m_project_interactor.scene_interactor().object_selection().elements.front();

        proj_ctx.rotation = Biz::Emboss::calc_rotation(project, element);
        dialog().set_rotation(proj_ctx.rotation.value_or(0.f));
    }

    if (!proj_ctx.exist_surface_point) {
        m_proj_ctxs->selected().exist_surface_point = true;
        // enable from surface distance in dialog
        const Domain::ModelVolume* volume_ptr = get_selected_svg_volume(
            m_project_interactor.selected_project(),
            m_project_interactor.scene_interactor().object_selection().elements
        );
        ASSERT(volume_ptr != nullptr); // cant activated without selected embossed svg volume
        update_svg_dialog(
            dialog(),
            proj_ctx,
            *volume_ptr
        ); // update dialog to new state with surface point
    }
    return true;
}

void SvgGizmo::on_drag_finish()
{
    m_surface_drag.on_drag_finish();
    if (m_proj_ctxs->selected().shape.projection.use_surface)
        update_volume();
}

void SvgGizmo::on_drag_cancel()
{
    m_surface_drag.on_drag_cancel();
}

void SvgGizmo::render_imgui()
{
    m_surface_drag.imgui_draw(); // cross hair during drag
}

namespace {
Domain::ModelVolume& get_selected_volume(Biz::ProjectInteractor& project_interactor)
{
    Domain::Project& project = project_interactor.selected_project();
    const Domain::ElementRef& el =
        project_interactor.scene_interactor().object_selection().elements.front();
    return (el.has_volume()) ? *project.find_volume_by_id(el.object_id, el.volume_id) :
                               *project.find_object_by_id(el.object_id)->volumes.front();
}

const Domain::ModelVolume*
get_selected_svg_volume(const Domain::Project& project, const Domain::ElementRefs& elements)
{
    if (elements.size() != 1)
        return nullptr; // multiple volumes selected

    const Domain::ElementRef& selected    = elements.front();
    const Domain::ModelVolume* volume_ptr = nullptr;
    if (selected.has_volume()) {
        volume_ptr = project.find_volume_by_id(selected.object_id, selected.volume_id);
    } else {
        // Check is selected object contain only volume with svg
        const Domain::ModelObject* object_ptr = project.find_object_by_id(selected.object_id);
        if (object_ptr == nullptr)
            return nullptr; // after delete volume
        if (object_ptr->volumes.size() != 1)
            return nullptr; // object with multiple volumes
        volume_ptr = object_ptr->volumes.front();
    }

    if (volume_ptr == nullptr)
        return nullptr;

    if (!volume_ptr->emboss_shape.has_value())
        return nullptr; // selected volume is not embossed

    if (volume_ptr->text_configuration.has_value())
        return nullptr; // selected volume is text

    return volume_ptr;
}

const Domain::ModelVolume* get_selected_svg_volume(const Biz::ProjectInteractor& project_interactor)
{
    const Domain::Project& project = project_interactor.selected_project();
    const Biz::Scene::ObjectSelection& selection =
        project_interactor.scene_interactor().object_selection();
    return get_selected_svg_volume(project, selection.elements);
}

} // namespace

void SvgGizmo::on_activated()
{
    // Register for scene changes
    Biz::Scene::SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();
    scene_interactor.add_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
    scene_interactor.add_listener<Biz::Scene::ISceneChangedListener>(this);

    // set current state of scene
    Domain::SelectionId project_id = m_project_interactor.selected_project_id();
    on_scene_selection_changed(project_id, scene_interactor.object_selection());
}

void SvgGizmo::on_deactivated()
{
    Biz::Scene::SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();
    scene_interactor.remove_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
    scene_interactor.remove_listener<Biz::Scene::ISceneChangedListener>(this);
    m_proj_ctxs->selected().last_loaded_volume_id = Domain::ObjectID{}; // invalid
}

bool SvgGizmo::enabled() const
{
    return get_selected_svg_volume(m_project_interactor) != nullptr;
}

void SvgGizmo::on_project_deactivated(size_t old_project_id)
{
    m_project_interactor.scene_interactor()
        .remove_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
}

Scene::ToolType SvgGizmo::type() const
{
    return Scene::ToolType::Svg;
}

bool SvgGizmo::allows_activation_by_double_click(const Scene::GizmoEventContext& ctx)
{
    const Biz::Scene::ObjectSelection& selection =
        m_project_interactor.scene_interactor().object_selection();
    if (selection.elements.size() != 1)
        return false; // allow only when svg is already selected

    const Domain::ElementRef& selected = selection.elements.front();

    // is double click on selected svg volume?
    const Domain::Project& project = m_project_interactor.selected_project();
    for (const App::Scene::NodePickResult& pick : ctx.pick_results()) {
        if (!pick.node->has_tag_of_type<Scene::SceneNodeTag>())
            continue; // ignore staff(node) infront of svg volume
        auto* tag = pick.node->tag_of_type<Scene::SceneNodeTag>();
        if (tag == nullptr)
            continue;
        const Domain::ModelVolume* volume_ptr =
            project.find_volume_by_id(tag->object_id, tag->volume_id);
        if (volume_ptr == nullptr)
            continue; // weird situation
        const Domain::ModelVolume& volume = *volume_ptr;
        if (!volume.emboss_shape.has_value())
            break; // it is not embossed svg
        if (volume.text_configuration.has_value())
            break; // it is text not svg

        if (selected.volume_id != tag->volume_id
            || selected.object_id != tag->object_id
            || selected.instance_id != tag->instance_id)
            break; // double click is on other volume, not selected one

        // double click is on selected text volume, allow activation
        return true;
    }
    return false;
}

namespace {
void set_dialog_surface_distance(SvgDialog& dialog, const ProjectContext& proj_ctx)
{
    double distance_in_mm = proj_ctx.from_surface.value_or(0.f);
    double max_distance   = 2 * proj_ctx.shape.projection.depth;
    if (const std::optional<float>& scale = proj_ctx.volume_scale.depth; scale.has_value())
        max_distance *= *scale;
    dialog.set_surface_distance(distance_in_mm, max_distance);
}

Domain::Vec2d get_world_size(const ProjectContext& proj_ctx)
{
    Domain::Vec2d size = Biz::Algorithms::BoundingBox::sizes(proj_ctx.shape_bb);
    const auto& scale  = proj_ctx.volume_scale;
    if (scale.width.has_value())
        size.x() *= (*scale.width);
    if (scale.height.has_value())
        size.y() *= (*scale.height);
    return size;
}

Domain::Vec2d round_thousandth(const Domain::Vec2d& vec)
{
    return Biz::Algorithms::Point::round(vec * 1000) / 1000.;
}

void update_svg_dialog(
    SvgDialog& dialog,
    const ProjectContext& proj_ctx,
    const Domain::ModelVolume& volume
)
{
    // disable callback till new values are set
    SvgDialog::Callbacks temp_callbacks = std::move(dialog.callbacks());
    dialog.callbacks()                  = SvgDialog::Callbacks{};
    ScopeGuard sg_callbacks([&dialog, &temp_callbacks]()
                            { dialog.callbacks() = std::move(temp_callbacks); });

    dialog.set_warning(proj_ctx.warning_tooltip);
    const Domain::EmbossShape& shape = proj_ctx.shape;
    dialog.set_shape(shape);
    dialog.set_enable_reload_from_disk(!shape.svg_file->path.empty());

    // Update dialog data
    const Domain::EmbossProjection& projection = shape.projection;
    double depth_in_mm = projection.depth * proj_ctx.volume_scale.depth.value_or(1.);
    dialog.set_depth(depth_in_mm);

    Domain::Vec2d size_original = Biz::Algorithms::BoundingBox::sizes(proj_ctx.shape_bb);
    Domain::Vec2d size          = get_world_size(proj_ctx);
    dialog.set_size(round_thousandth(size), round_thousandth(size_original));
    dialog.set_size_lock(!proj_ctx.is_size_locked);

    bool is_part = volume.get_object()->volumes.size() != 1;
    dialog.set_enable_use_surface(is_part);
    bool use_surface = projection.use_surface;
    dialog.set_use_surface(use_surface);

    dialog.set_enable_surface_distance(is_part && !use_surface && proj_ctx.exist_surface_point);
    set_dialog_surface_distance(dialog, proj_ctx);
    dialog.set_rotation(proj_ctx.rotation.value_or(0.f));
    dialog.set_rotation_lock(!proj_ctx.up_limit.has_value());
    dialog.show_part_specific_panel(is_part);
    if (is_part) {
        dialog.set_operation(volume.type());
    }
}

// True when exist change in scale otherwise false
bool calc_scales(Scale& volume_scale, const Domain::ModelVolume& volume)
{
    const Domain::ModelInstance& instance = *volume.get_object()->instances.front();
    Domain::Transform3d to_world          = instance.get_matrix() * volume.get_matrix();
    auto to_world_linear                  = to_world.linear();
    auto calc = [&to_world_linear](const Domain::Vec3d& axe, std::optional<float>& scale)
    {
        Domain::Vec3d axe_world = to_world_linear * axe;
        double norm_sq          = axe_world.squaredNorm();
        if (Domain::is_approx(norm_sq, 1.)) {
            if (scale.has_value())
                scale.reset();
            else
                return false;
        } else {
            scale = sqrt(norm_sq);
        }
        return true;
    };

    bool exist_change = calc(Domain::Vec3d::UnitY(), volume_scale.height);
    exist_change |= calc(Domain::Vec3d::UnitX(), volume_scale.width);
    exist_change |= calc(Domain::Vec3d::UnitZ(), volume_scale.depth);
    return exist_change;
}

bool is_closed(NSVGpath* path)
{
    for (; path != NULL; path = path->next)
        if (path->next == NULL && path->closed)
            return true;
    return false;
}

void add_comma_separated(std::string& result, const std::string& add)
{
    if (!result.empty())
        result += ", ";
    result += add;
}

const float warning_preccission = 1e-4f;

std::string create_fill_warning(const NSVGshape& shape)
{
    if (shape.flags != NSVG_FLAGS_VISIBLE || shape.fill.type == NSVG_PAINT_NONE)
        return {}; // not visible

    std::string warning;
    if ((shape.opacity - 1.f + warning_preccission) <= 0.f)
        add_comma_separated(
            warning,
            fmt::format(fmt::runtime(_u8L("Opacity ({})")), shape.opacity)
        );

    // if(shape->flags != NSVG_FLAGS_VISIBLE) add_warning(_u8L("Visibility flag"));
    bool is_fill_gradient = shape.fillGradient[0] != '\0';
    if (is_fill_gradient)
        add_comma_separated(
            warning,
            fmt::format(fmt::runtime(_u8L("Color gradient ({})")), shape.fillGradient)
        );

    switch (shape.fill.type) {
    case NSVG_PAINT_UNDEF:
        add_comma_separated(warning, _u8L("Undefined fill type"));
        break;
    case NSVG_PAINT_LINEAR_GRADIENT:
        if (!is_fill_gradient)
            add_comma_separated(warning, _u8L("Linear gradient"));
        break;
    case NSVG_PAINT_RADIAL_GRADIENT:
        if (!is_fill_gradient)
            add_comma_separated(warning, _u8L("Radial gradient"));
        break;
        // case NSVG_PAINT_NONE:
        // case NSVG_PAINT_COLOR:
        // default: break;
    }

    // Unfilled is only line which could be opened
    if (shape.fill.type != NSVG_PAINT_NONE && !is_closed(shape.paths))
        add_comma_separated(warning, _u8L("Open filled path"));
    return warning;
}

std::string create_stroke_warning(const NSVGshape& shape, float min_scale)
{
    std::string warning;
    if (shape.flags != NSVG_FLAGS_VISIBLE
        || shape.stroke.type == NSVG_PAINT_NONE
        || shape.strokeWidth <= 1e-5f)
        return {}; // not visible

    if (float minimal_width_in_mm = 1e-3f; shape.strokeWidth <= minimal_width_in_mm * min_scale) {
        add_comma_separated(
            warning,
            fmt::format(
                fmt::runtime(_u8L("Too thin stroke (minimal width is {} mm).")),
                minimal_width_in_mm
            )
        );
    }

    if ((shape.opacity - 1.f + warning_preccission) <= 0.f)
        add_comma_separated(
            warning,
            fmt::format(fmt::runtime(_u8L("Opacity ({})")), shape.opacity)
        );

    bool is_stroke_gradient = shape.strokeGradient[0] != '\0';
    if (is_stroke_gradient)
        add_comma_separated(
            warning,
            fmt::format(fmt::runtime(_u8L("Color gradient ({})")), shape.strokeGradient)
        );

    switch (shape.stroke.type) {
    case NSVG_PAINT_UNDEF:
        add_comma_separated(warning, _u8L("Undefined stroke type"));
        break;
    case NSVG_PAINT_LINEAR_GRADIENT:
        if (!is_stroke_gradient)
            add_comma_separated(warning, _u8L("Linear gradient"));
        break;
    case NSVG_PAINT_RADIAL_GRADIENT:
        if (!is_stroke_gradient)
            add_comma_separated(warning, _u8L("Radial gradient"));
        break;
        // case NSVG_PAINT_COLOR:
        // case NSVG_PAINT_NONE:
        // default: break;
    }
    return warning;
}

// NOTE: with NanoSVG is impossible to inform user about unsupported features inside SVG file like:
// * Text
// * Blur
// * Patterns in infill
std::string create_shape_warnings(const Domain::EmbossShape& shape, float min_scale)
{
    const std::shared_ptr<NSVGimage>& image_ptr = shape.svg_file->image;
    assert(image_ptr != nullptr);
    if (image_ptr == nullptr)
        return {std::string{"Uninitialized SVG image"}};

    const NSVGimage& image = *image_ptr;
    std::vector<std::string> result;
    auto add_warning = [&result, &image](size_t index, const std::string& message)
    {
        if (result.empty())
            result = std::vector<std::string>(Biz::Emboss::get_shapes_count(image) * 2);
        std::string& res = result[index];
        if (res.empty())
            res = message;
        else
            res += '\n' + message;
    };
    if (!shape.final_shape.is_healed) {
        for (const Domain::ExPolygonsWithId& i : shape.shapes_with_ids)
            if (!i.is_healed)
                add_warning(
                    i.id,
                    _u8L("Path can't be healed from selfintersection and multiple points.")
                );

        // This waning is not connected to NSVGshape. It is about union of paths, but Zero index is shown first
        size_t index = 0;
        add_warning(
            index,
            _u8L("Final shape constains selfintersection or multiple points with same coordinate.")
        );
    }

    size_t shape_index = 0;
    for (NSVGshape* nsvg_shape = image.shapes; nsvg_shape != NULL;
         nsvg_shape            = nsvg_shape->next, ++shape_index)
    {
        if (!(nsvg_shape->flags == NSVG_FLAGS_VISIBLE)) {
            add_warning(
                shape_index * 2,
                fmt::format(
                    fmt::runtime(_u8L("Shape is marked as invisible ({}).")),
                    nsvg_shape->id
                )
            );
            continue;
        }
        if (std::string fill_warning = create_fill_warning(*nsvg_shape); !fill_warning.empty()) {
            // TRN: The first placeholder is shape identifier, the second one is text describing the problem.
            add_warning(
                shape_index * 2,
                fmt::format(
                    fmt::runtime(_u8L("Fill of shape ({}) contains unsupported: {}.")),
                    nsvg_shape->id,
                    fill_warning
                )
            );
        }
        if (std::string stroke_warning = create_stroke_warning(*nsvg_shape, min_scale);
            !stroke_warning.empty())
            add_warning(
                shape_index * 2 + 1,
                fmt::format(
                    fmt::runtime(_u8L("Stroke of shape ({}) contains unsupported: {}.")),
                    nsvg_shape->id,
                    stroke_warning
                )
            );
    }

    // convert result to one multiline message:
    std::string warnings;
    for (const std::string& warning : result) {
        if (warning.empty())
            continue;
        if (!warnings.empty())
            warnings += "\n";
        warnings += warning;
    }
    return warnings;
}

void calc_from_surface(ProjectContext& proj_ctx, const Domain::Project& project,
    const Domain::ElementRef& ref, PlaterScenePresenter& scene_presenter) {
    Scene::Node& root = scene_presenter.scene().root();
    auto distance_exp = Biz::Emboss::calc_distance(project, ref, root);
    proj_ctx.exist_surface_point =
        distance_exp.has_value() || distance_exp.error() == Biz::Emboss::DistanceIssue::ApproxZero;
    proj_ctx.from_surface =
        distance_exp.has_value() ? std::optional<float>{*distance_exp} : std::optional<float>{};
}
} // namespace

void SvgGizmo::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection
)
{
    update_from_selected_elements(project_id, selection.elements);
}

void
SvgGizmo::on_volume_mesh_changed(Domain::SelectionId project_id, const Domain::ElementRefs& volumes)
{
    update_from_selected_elements(project_id, volumes);
}

void SvgGizmo::update_from_selected_elements(
    Domain::SelectionId project_id,
    const Domain::ElementRefs& selected_elements
)
{
    const Domain::Project& project        = m_project_interactor.project(project_id);
    const Domain::ModelVolume* volume_ptr = get_selected_svg_volume(project, selected_elements);
    if (volume_ptr == nullptr)
        return close(); // unselection text volume

    const Domain::ModelVolume& volume = *volume_ptr;
    ProjectContext& proj_ctx          = m_proj_ctxs->project(project_id);
    if (proj_ctx.last_loaded_volume_id == volume.id())
        return; // already loaded
    proj_ctx.last_loaded_volume_id = volume.id();
    proj_ctx.shape                 = *volume.emboss_shape; // copy for edit
    calc_scales(proj_ctx.volume_scale, volume);
    const Domain::ModelObject& object = *volume.get_object();
    const Domain::ModelInstance* instance =
        Biz::Emboss::get_selected_instance(selected_elements, project);
    ASSERT(instance != nullptr); // should be impossible without instance
    ASSERT(instance->get_object() == &object);
    Domain::ElementRef ref(object.id().id, instance->id().id, volume.id().id);
    proj_ctx.rotation = Biz::Emboss::calc_rotation(project, ref);
    bool is_part      = object.volumes.size() != 1;
    proj_ctx.from_surface.reset();
    if (is_part)
        calc_from_surface(proj_ctx, project, ref, m_scene_presenter);

    if (proj_ctx.shape.shapes_with_ids.empty()) {
        // first open of embossed SVG loaded from 3mf
        // Need to process svg file data
        SvgShapeProvider provider(proj_ctx.shape, proj_ctx.volume_scale);
        provider.create_shape_with_union();
        proj_ctx.shape = provider.get_shape();
    }
    ASSERT(!proj_ctx.shape.shapes_with_ids.empty());
    Domain::BoundingBox2crd bb_crd =
        Biz::Algorithms::ExPolygonsWithId::get_extents(proj_ctx.shape.shapes_with_ids);
    proj_ctx.shape_bb = Domain::BoundingBox2d{
        bb_crd.min.cast<double>() * proj_ctx.shape.scale,
        bb_crd.max.cast<double>() * proj_ctx.shape.scale
    };

    const auto& scale        = proj_ctx.volume_scale;
    float min_scale          = std::min(scale.width.value_or(1.), scale.height.value_or(1.));
    proj_ctx.warning_tooltip = create_shape_warnings(proj_ctx.shape, min_scale);
    update_svg_dialog(dialog(), proj_ctx, volume);
}

void SvgGizmo::on_project_activated(size_t new_project_id)
{
    Biz::Scene::SceneInteractor& scene_interactor = m_project_interactor.scene_interactor();
    scene_interactor.add_listener<Biz::Scene::ISceneSelectionChangedListener>(this);

    // fill dialog with current data
    const Domain::Project& project = m_project_interactor.project(new_project_id);
    const Domain::ModelVolume* volume_ptr =
        get_selected_svg_volume(project, scene_interactor.object_selection().elements);
    ASSERT(volume_ptr != nullptr); // cant activated without selected embossed svg volume

    update_svg_dialog(dialog(), m_proj_ctxs->project(new_project_id), *volume_ptr);
}

namespace {
Biz::Emboss::BaseData::IssueFn create_issue_fn(
    SvgDialog& dialog,
    std::string& warning_tooltip,
    const Biz::ProjectInteractor& project_interactor
)
{
    auto prepend_tooltip =
        [&dialog,
         &warning_tooltip,
         &project_interactor,
         project_id = project_interactor.selected_project_id()](const std::string& message)
    {
        std::string prev_tooltip =
            (warning_tooltip.empty()) ? std::string() : ("\n" + warning_tooltip);
        warning_tooltip = message + prev_tooltip;
        if (project_id == project_interactor.selected_project_id())
            dialog.set_warning(warning_tooltip);
    };
    return [prepend_tooltip](Biz::Emboss::JobIssue issue)
    {
        using namespace Slic3r::Biz::Emboss; // JobIssue
        switch (issue) {
        case JobIssue::no_shape:
            App::AppServices::instance().dialog_manager().show_error_dialog(
                _u8L("No shape, check SVG that contain path.")
            );
            break;
        case JobIssue::no_surface:
            prepend_tooltip(_u8L("No surface, check correct position of the SVG."));
            break;
        case JobIssue::canceled: /* prepend_tooltip(_u8L("Job was canceled.")); */
            break;
        default:
            prepend_tooltip(_u8L("Emboss job was not finished."));
        }
    };
}

Biz::Emboss::BaseData create_base_data(
    Domain::ModelVolumeType volume_type,
    const ProjectContext& proj_ctx,
    Biz::ProjectInteractor& project_interactor,
    Biz::Emboss::BaseData::IssueFn issue_fn
)
{
    Domain::SelectionId project_id = project_interactor.selected_project_id();
    return Biz::Emboss::BaseData{
        .tri_mesh = {
            .shape_provider = std::make_unique<SvgShapeProvider>(proj_ctx.shape, proj_ctx.volume_scale),
            .is_outside = (volume_type == Domain::ModelVolumeType::MODEL_PART),
        },
        .project_interactor = project_interactor,
        .project_id = project_id,
         //.volume_name = get_filename(*proj_ctx.shape.svg_file), -> "do not reset name"
        .issue_fn = std::move(issue_fn)
    };
}
} // namespace

bool SvgGizmo::update_volume(std::optional<Domain::ModelVolumeType> volume_type)
{
    const Domain::ModelVolume* volume_ptr = get_selected_svg_volume(m_project_interactor);
    ASSERT(volume_ptr != nullptr); // no volume selected
    const Domain::ModelVolume& volume = *volume_ptr;
    const Domain::ModelInstance* instance_ptr =
        Biz::Emboss::get_selected_instance(m_project_interactor);
    ASSERT(instance_ptr != nullptr);
    ProjectContext& proj_ctx = m_proj_ctxs->selected();

    // check that selection did not change without call 'on_scene_selection_changed()'
    ASSERT(proj_ctx.last_loaded_volume_id == volume.id());

    Domain::ModelVolumeType new_type = volume_type.value_or(volume.type());
    auto issue_fn = create_issue_fn(dialog(), proj_ctx.warning_tooltip, m_project_interactor);
    Biz::Emboss::UpdateVolumeParams params{
        .base        = create_base_data(new_type, proj_ctx, m_project_interactor, issue_fn),
        .volume_id   = volume.id(),
        .instance_id = instance_ptr->id(),
        .volume_type = volume_type
    };
    return start_update_volume(std::move(params), volume);
}

void SvgGizmo::close()
{
    m_gizmo_controller.deactivate_current_tool();
}
} // namespace Slic3r::App::Plater
