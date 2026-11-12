#include "Slic3r/App/Plater/PaintOnSupportsGizmo.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/DisplayStrings.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/Plater/PaintOnGizmoBase.hpp"
#include "Slic3r/App/Plater/PaintOnSupportsDialog.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Scene/Clipper.hpp"
#include "Slic3r/App/Scene/ClipperPresenter.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Biz/Algorithms/TriangleSelectorPainter.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/StatusCache.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"

#include "libslic3r/GeneratedSupportPoints.hpp"

#include <algorithm>
#include <functional>
#include <optional>

using Slic3r::Biz::GeneratedSupportPointsCache;
using Slic3r::Biz::IMessageDialogProvider;
using Slic3r::Biz::ObjectSupportPointsRef;
using Slic3r::Biz::UndoSnapshotType;
using Slic3r::Biz::Algorithms::TriangleSelector;
using Slic3r::Biz::Scene::ObjectSelection;
using Slic3r::Biz::Slicing::GeneratedSupportPoint;
using Slic3r::Biz::Slicing::ObjectSupportPoints;
using Slic3r::Biz::Slicing::SlicingInteractor;
using Slic3r::Biz::Slicing::StatusCode;
using Slic3r::Domain::BedRef;
using Slic3r::Domain::FacetsAnnotationKind;
using Slic3r::Domain::ModelInstance;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelVolume;
using Slic3r::Domain::ObjectID;
using Slic3r::Domain::Project;
using Slic3r::Domain::SelectionId;
using Slic3r::Domain::SlicingId;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

namespace Slic3r::Biz {

/**
 * @brief Provides generated support points for one model object from a cache or by requesting slicing.
 */
class GeneratedSupportPointsRequest :
    public IGeneratedSupportPointsCacheChangedListener,
    public IStatusCacheChangedListener
{
public:
    struct Callbacks
    {
        std::function<void(std::optional<ObjectSupportPointsRef>)> completed =
            [](std::optional<ObjectSupportPointsRef>) {};
    };

    GeneratedSupportPointsRequest() = delete;

    GeneratedSupportPointsRequest(
        SlicingInteractor& slicing_interactor,
        StatusCache& status_cache,
        GeneratedSupportPointsCache& support_points_cache
    ) :
        m_slicing_interactor(slicing_interactor),
        m_status_cache(status_cache),
        m_support_points_cache(support_points_cache)
    {}

    ~GeneratedSupportPointsRequest() override
    {
        this->cancel();
    }

    Callbacks& callbacks()
    {
        return m_callbacks;
    }

    void start(SlicingId slicing_id, ObjectID model_object_id)
    {
        if (this->running()) {
            return;
        }

        m_state            = State::WaitingForSlicing;
        m_slicing_id       = slicing_id;
        m_model_object_id  = model_object_id;
        m_has_fresh_points = false;
        m_support_points_cache.add_listener<IGeneratedSupportPointsCacheChangedListener>(this);
        m_status_cache.add_listener<IStatusCacheChangedListener>(this);

        const StatusCode status = m_slicing_interactor.get_status(slicing_id);
        if (status == StatusCode::Finished) {
            this->complete(this->cached_support_points());
        } else if (status == StatusCode::Modified) {
            this->request_slicing_until_support_spots();
        } else if (status == StatusCode::Empty || status == StatusCode::InvalidData) {
            this->complete(std::nullopt);
        }
    }

    void cancel()
    {
        if (!this->running()) {
            return;
        }

        m_support_points_cache.remove_listener<IGeneratedSupportPointsCacheChangedListener>(this);
        m_status_cache.remove_listener<IStatusCacheChangedListener>(this);

        m_state = State::Idle;
    }

    [[nodiscard]] bool running() const
    {
        return m_state != State::Idle;
    }

    void on_generated_support_points_cache_changed(const SlicingId id) override
    {
        if (!this->running() || id != m_slicing_id) {
            return;
        }

        const std::optional<Slicing::Status> current_status = m_status_cache.get_status(id);
        if (current_status.has_value()
            && current_status->code == StatusCode::Running
            && this->cached_support_points().has_value())
        {
            m_has_fresh_points = true;
        }
    }

    void on_status_cache_status_code_changed(const SlicingId id) override
    {
        if (!this->running() || id != m_slicing_id) {
            return;
        }

        const std::optional<Slicing::Status> status = m_status_cache.get_status(id);
        if (!status.has_value()) {
            this->complete(std::nullopt);
            return;
        }

        switch (status->code) {
        case StatusCode::Running:
            m_state            = State::SlicingActive;
            m_has_fresh_points = false;
            break;
        case StatusCode::Stopping:
            m_state            = State::SlicingActive;
            m_has_fresh_points = false;
            break;
        case StatusCode::Updating:
            m_has_fresh_points = false;
            break;
        case StatusCode::Modified:
            if (m_has_fresh_points) {
                this->complete(this->cached_support_points());
            } else if (m_state == State::WaitingForSlicing) {
                this->request_slicing_until_support_spots();
            } else {
                this->complete(std::nullopt);
            }
            break;
        case StatusCode::Finished:
            this->complete(this->cached_support_points());
            break;
        case StatusCode::Empty:
        case StatusCode::InvalidData:
            this->complete(std::nullopt);
            break;
        default:
            break;
        }
    }

private:
    enum class State
    {
        Idle,
        WaitingForSlicing,
        SlicingRequested,
        SlicingActive
    };

    [[nodiscard]] std::optional<ObjectSupportPointsRef> cached_support_points() const
    {
        return m_support_points_cache.get_object_support_points(m_slicing_id, m_model_object_id);
    }

    void complete(std::optional<ObjectSupportPointsRef> support_points)
    {
        this->cancel();
        m_callbacks.completed(support_points);
    }

    void request_slicing_until_support_spots()
    {
        m_state = State::SlicingRequested;
        m_slicing_interactor.slice_bed(
            m_slicing_id,
            Slicing::SliceUntilStep{posSupportSpotsSearch, m_model_object_id}
        );
    }

    SlicingInteractor& m_slicing_interactor;
    StatusCache& m_status_cache;
    GeneratedSupportPointsCache& m_support_points_cache;

    Callbacks m_callbacks;

    State m_state           = State::Idle;
    bool m_has_fresh_points = false;
    SlicingId m_slicing_id;
    ObjectID m_model_object_id;
};

} // namespace Slic3r::Biz

namespace Slic3r::App::Plater {

PaintOnSupportsGizmo::PaintOnSupportsGizmo(
    Render::Device& device,
    Scene::GeometryDataFactory& data_factory,
    Biz::ProjectInteractor& project_interactor,
    PlaterScenePresenter& scene_presenter
) :
    PaintOnGizmoBase(device, data_factory, project_interactor, scene_presenter)
{
    m_support_points_request = std::make_unique<GeneratedSupportPointsRequest>(
        project_interactor.slicing_interactor(),
        project_interactor.status_cache(),
        project_interactor.generated_support_points_cache()
    );

    m_dialog = std::make_unique<PaintOnSupportsDialog>();
    m_dialog->set_tool_type(m_tool_type);
    m_dialog->set_brush_type(m_cursor_type);
    m_dialog->set_brush_radius(m_cursor_radius);
    m_dialog->set_smart_fill_angle(m_smart_fill_angle);
    m_dialog->set_clipping_of_view_value(0.);
    m_dialog->set_highlight_overhangs_angle(m_highlight_by_angle_threshold_deg);
    m_dialog->set_paint_on_overhangs_only_value(m_paint_on_overhangs_only);
    m_dialog->set_split_triangles_value(m_triangle_splitting_enabled);
    m_dialog->set_automatic_painting_running(m_automatic_painting_slicing_id.has_value());

    m_dialog->callbacks().tool_type_changed = [this](const PaintOnGizmoBase::ToolType tool_type)
    { this->set_tool_type(tool_type); };

    m_dialog->callbacks().brush_shape_changed =
        [this](const Biz::Algorithms::TriangleSelector::CursorType cursor_type)
    { this->set_cursor_type(cursor_type); };

    m_dialog->callbacks().brush_radius_changed = [this](const double value)
    { m_cursor_radius = static_cast<float>(value); };

    m_dialog->callbacks().smart_fill_angle_changed = [this](const double value)
    { m_smart_fill_angle = static_cast<float>(value); };

    m_dialog->callbacks().clipping_of_view_value_changed = [this](double value)
    {
        m_clipping_plane_presenter.set_position_by_ratio(value, true);
        this->update_clipping_plane();
    };

    m_dialog->callbacks().clipping_of_view_reset_direction = [this]()
    {
        m_clipping_plane_presenter.set_position_by_ratio(-1, false);
        this->update_clipping_plane();
    };

    m_dialog->callbacks().highlight_overhangs_angle_changed = [this](double value)
    {
        m_highlight_by_angle_threshold_deg = static_cast<float>(value);
        this->update_overhang_detection();
    };

    m_dialog->callbacks().overhangs_enforced = [this]()
    {
        this->select_facets_by_angle(m_highlight_by_angle_threshold_deg);

        m_highlight_by_angle_threshold_deg = 0.f;
        m_dialog->set_highlight_overhangs_angle(m_highlight_by_angle_threshold_deg);
    };

    m_dialog->callbacks().paint_on_overhangs_only_value_changed = [this](const bool value)
    { m_paint_on_overhangs_only = value; };

    m_dialog->callbacks().split_triangles_value_changed = [this](const bool value)
    { m_triangle_splitting_enabled = value; };

    m_dialog->callbacks().automatic_painting = [this]() { this->auto_generate_support_painting(); };

    m_dialog->callbacks().painting_reset = [this]()
    {
        this->clear_all_paintings();
        m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::PaintOnSupportsStroke);
    };

    m_support_points_request->callbacks().completed =
        [this](const std::optional<ObjectSupportPointsRef> support_points)
    { this->on_support_points_request_completed(support_points); };
}

PaintOnSupportsGizmo::~PaintOnSupportsGizmo() = default;

void PaintOnSupportsGizmo::on_deactivated()
{
    if (m_automatic_painting_slicing_id.has_value()) {
        m_support_points_request->cancel();
        this->finish_automatic_painting(false);
    }

    PaintOnGizmoBase::on_deactivated();
}

void PaintOnSupportsGizmo::on_model_reloaded(const SelectionId project_id)
{
    if (project_id == m_project_interactor.selected_project_id()) {
        this->cancel_automatic_painting();
    }

    PaintOnGizmoBase::on_model_reloaded(project_id);
}

void PaintOnSupportsGizmo::on_scene_selection_changed(
    const SelectionId project_id,
    const ObjectSelection& selection
)
{
    this->cancel_automatic_painting();

    PaintOnGizmoBase::on_scene_selection_changed(project_id, selection);
}

Scene::ToolType PaintOnSupportsGizmo::type() const
{
    return Scene::ToolType::PaintOnSupportsGizmo;
}

GizmoWindowPtr PaintOnSupportsGizmo::release_ui_window()
{
    return m_dialog.release();
}

FacetsAnnotationKind PaintOnSupportsGizmo::get_facets_annotation_kind() const
{
    return FacetsAnnotationKind::FdmSupports;
}

const Domain::FacetsAnnotation& PaintOnSupportsGizmo::get_facets_annotation(
    const Domain::ModelVolume& model_volume
) const
{
    return model_volume.supported_facets;
}

bool PaintOnSupportsGizmo::set_facets_annotation(
    Domain::ModelVolume& model_volume,
    const Biz::Algorithms::TriangleSelector& triangle_selector
) const
{
    const bool result{model_volume.supported_facets.set_data(triangle_selector.serialize())};
    return result;
}

void PaintOnSupportsGizmo::on_painting_stroke_applied()
{
    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::PaintOnSupportsStroke);
}

Domain::TriangleSelector::TriangleStateType PaintOnSupportsGizmo::get_left_button_state_type() const
{
    return Domain::TriangleSelector::TriangleStateType::ENFORCER;
}

Domain::TriangleSelector::TriangleStateType
PaintOnSupportsGizmo::get_right_button_state_type() const
{
    return Domain::TriangleSelector::TriangleStateType::BLOCKER;
}

void PaintOnSupportsGizmo::on_cursor_radius_changed(float value)
{
    m_dialog->set_brush_radius(static_cast<double>(value));
}

void PaintOnSupportsGizmo::on_smart_fill_angle_changed(float value)
{
    m_dialog->set_smart_fill_angle(static_cast<double>(value));
}

void PaintOnSupportsGizmo::on_clipping_of_view_changed(double value)
{
    m_dialog->set_clipping_of_view_value(static_cast<double>(value));
}

void PaintOnSupportsGizmo::select_facets_by_angle(const float threshold_deg)
{
    const float threshold = (std::numbers::pi_v<float> / 180.f) * threshold_deg;

    for (const PaintableVolume& paintable_volume : m_paintable_volumes) {
        const size_t volume_idx             = &paintable_volume - &m_paintable_volumes.front();
        const ModelInstance& model_instance = paintable_volume.model_instance;
        const ModelVolume& model_volume     = paintable_volume.model_volume;
        TriangleSelectorRenderWrapper& triangle_selector_wrappers =
            m_triangle_selector_wrappers[volume_idx];
        TriangleSelector& triangle_selector = triangle_selector_wrappers.triangle_selector();

        const Transform3d trafo_matrix =
            model_instance.get_matrix_no_offset() * model_volume.get_matrix_no_offset();
        const Vec3f down = (trafo_matrix.inverse() * (-Vec3d::UnitZ())).cast<float>().normalized();
        const Vec3f limit =
            (trafo_matrix.inverse() * Vec3d(std::sin(threshold), 0, -std::cos(threshold)))
                .cast<float>()
                .normalized();
        const float dot_limit = limit.dot(down);

        // Now calculate dot product of vert_direction and facets' normals.
        const indexed_triangle_set& its = model_volume.mesh().its;
        for (const stl_triangle_vertex_indices& face : its.indices) {
            if (Algorithms::TriangleMesh::its_face_normal(its, face).dot(down) > dot_limit) {
                const size_t facet_idx = &face - &its.indices.front();
                triangle_selector.set_facet(
                    facet_idx,
                    Domain::TriangleSelector::TriangleStateType::ENFORCER
                );
            }
        }

        triangle_selector_wrappers.update_painted_geometry(m_device);
    }

    this->apply_painting_to_model();
    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::PaintOnSupportsStroke);
}

void PaintOnSupportsGizmo::auto_generate_support_painting()
{
    using Slicing::StatusCode;

    if (m_automatic_painting_slicing_id.has_value() || m_paintable_volumes.empty()) {
        return;
    }

    const Project& project              = m_project_interactor.selected_project();
    const ModelObject& model_object     = m_paintable_volumes.front().model_object;
    const ModelInstance& model_instance = m_paintable_volumes.front().model_instance;

    if (!model_instance.is_printable()) {
        this->show_printable_object_required_warning();
        return;
    }

    const BedRef bed_ref = model_instance.get_last_bed();
    if (project.find_bed_instance_by_id(bed_ref.instance_id) == nullptr) {
        this->show_object_not_on_bed_warning();
        return;
    }

    const SlicingId slicing_id{m_project_interactor.selected_project_id(), bed_ref.instance_id};
    const StatusCode status = m_project_interactor.slicing_interactor().get_status(slicing_id);
    if (status == StatusCode::InvalidData) {
        this->show_invalid_print_setup_warning(slicing_id);
        return;
    } else if (status == StatusCode::Empty) {
        this->show_printable_object_required_warning();
        return;
    }

    const bool not_painted = std::ranges::all_of(
        m_paintable_volumes,
        [this](const PaintableVolume& paintable_volume)
        { return this->get_facets_annotation(paintable_volume.model_volume).empty(); }
    );

    if (not_painted) {
        this->start_automatic_painting(slicing_id, model_object.id());
        return;
    }

    IMessageDialogProvider& dialog_manager = AppServices::instance().dialog_manager();
    dialog_manager.show_yesno_dialog(
        _u8L("Warning"),
        _u8L("Automatic painting will erase all currently painted areas.")
            + "\n\n"
            + _u8L("Are you sure you want to do it?"),
        [this, slicing_id, model_object_id = model_object.id()](const bool answer)
        {
            if (!answer
                || m_automatic_painting_slicing_id.has_value()
                || m_paintable_volumes.empty()
                || m_paintable_volumes.front().model_object.id() != model_object_id)
            {
                return;
            }

            this->start_automatic_painting(slicing_id, model_object_id);
        }
    );
}

void PaintOnSupportsGizmo::start_automatic_painting(
    const SlicingId slicing_id,
    const ObjectID model_object_id
)
{
    m_automatic_painting_slicing_id = slicing_id;

    m_dialog->set_automatic_painting_running(true);
    m_support_points_request->start(slicing_id, model_object_id);
}

void PaintOnSupportsGizmo::on_support_points_request_completed(
    const std::optional<ObjectSupportPointsRef> support_points
)
{
    ASSERT(m_automatic_painting_slicing_id.has_value());

    const SlicingId slicing_id = *m_automatic_painting_slicing_id;

    const bool applied =
        support_points.has_value() && this->apply_generated_support_points(slicing_id);

    const std::optional<Slicing::Status> status =
        m_project_interactor.status_cache().get_status(slicing_id);
    this->finish_automatic_painting(applied);

    if (status.has_value() && status->code == Slicing::StatusCode::InvalidData) {
        this->show_invalid_print_setup_warning(slicing_id);
    }
}

bool PaintOnSupportsGizmo::apply_generated_support_points(const SlicingId slicing_id)
{
    const GeneratedSupportPointsCache& support_points_cache =
        m_project_interactor.generated_support_points_cache();

    bool applied = false;
    for (const PaintableVolume& paintable_volume : m_paintable_volumes) {
        const size_t volume_idx = &paintable_volume - &m_paintable_volumes.front();
        const std::optional<ObjectSupportPointsRef> object_support_points_ref =
            support_points_cache.get_object_support_points(
                slicing_id,
                paintable_volume.model_object.id()
            );

        if (!object_support_points_ref.has_value()) {
            continue;
        }

        const ObjectSupportPoints& object_support_points = object_support_points_ref->get();
        const Transform3d mesh_transform =
            object_support_points.object_transform * paintable_volume.model_volume.get_matrix();
        Algorithms::TriangleSelectorPainter triangle_selector_painter{
            paintable_volume.model_volume.mesh(),
            mesh_transform
        };

        for (const GeneratedSupportPoint& support_point : object_support_points.support_points) {
            triangle_selector_painter.paint_spot(
                support_point.position,
                support_point.spot_radius,
                Domain::TriangleSelector::TriangleStateType::ENFORCER
            );
        }

        TriangleSelectorRenderWrapper& triangle_selector_wrapper =
            m_triangle_selector_wrappers[volume_idx];
        triangle_selector_wrapper.triangle_selector().deserialize(
            triangle_selector_painter.serialize(),
            true
        );
        triangle_selector_wrapper.update_painted_geometry(m_device);
        applied = true;
    }

    if (applied) {
        this->apply_painting_to_model();
    }

    return applied;
}

void PaintOnSupportsGizmo::finish_automatic_painting(const bool applied_support_points)
{
    ASSERT(m_automatic_painting_slicing_id.has_value());
    if (applied_support_points) {
        m_project_interactor.undo_provider().take_snapshot(
            UndoSnapshotType::PaintOnSupportsAutomaticPainting
        );
    }

    this->reset_automatic_painting_state();
}

void PaintOnSupportsGizmo::reset_automatic_painting_state()
{
    m_automatic_painting_slicing_id.reset();
    m_dialog->set_automatic_painting_running(false);
}

void PaintOnSupportsGizmo::cancel_automatic_painting()
{
    if (!m_automatic_painting_slicing_id.has_value()) {
        return;
    }

    m_support_points_request->cancel();
    this->reset_automatic_painting_state();
}

void PaintOnSupportsGizmo::show_invalid_print_setup_warning(const SlicingId slicing_id) const
{
    std::string error_message;
    const std::optional<Slicing::Status> status =
        m_project_interactor.status_cache().get_status(slicing_id);
    if (status.has_value()) {
        for (const Slicing::Error& error : status->errors) {
            error_message +=
                "\n" + App::to_display_string(error, m_project_interactor.selected_project());
        }
    }

    AppServices::instance().dialog_manager().show_warning_dialog(
        _u8L("Automatic painting requires valid print setup.") + error_message,
        _u8L("Warning")
    );
}

void PaintOnSupportsGizmo::show_printable_object_required_warning() const
{
    AppServices::instance().dialog_manager().show_warning_dialog(
        _u8L("Automatic painting requires printable object."),
        _u8L("Warning")
    );
}

void PaintOnSupportsGizmo::show_object_not_on_bed_warning() const
{
    AppServices::instance().dialog_manager().show_warning_dialog(
        _u8L("Automatic painting requires the object to be placed on a bed."),
        _u8L("Warning")
    );
}

} // namespace Slic3r::App::Plater
