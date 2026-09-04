#pragma once

#include <map>
#include <boost/functional/hash.hpp>

#include <Slic3r/Domain/SelectionId.hpp>
#include <Slic3r/Biz/Platform/ListenerList.hpp>
#include <Slic3r/Biz/Platform/IMainThreadDispatcher.hpp>
#include <Slic3r/Biz/ISelectedProjectChangedListener.hpp>
#include <Slic3r/Biz/Platform/PlatformServices.hpp>
#include <Slic3r/Domain/ConfigContainer.hpp>
#include <Slic3r/Biz/Platform/WithListeners.hpp>

#include "BackgroundProcess.hpp"
#include "libslic3r/IThumbnailImageGenerator.hpp"

namespace Slic3r::Biz::Slicing::Sla {
struct Object;
} // namespace Slic3r::Biz::Slicing::Sla

namespace Slic3r::Biz::Slicing {
struct SLAResult;

class ISlicingListener
{
public:
    virtual ~ISlicingListener() = default;
};

class IFDMResultListener : public ISlicingListener
{
public:
    virtual void on_fdm_result_changed(FDMResult&&, const Domain::SlicingId) = 0;
};

class ISLAResultListener : public ISlicingListener
{
public:
    virtual void on_sla_result_changed(const Domain::SlicingId&, SLAResult&&) = 0;
};

class ISLAObjectListener : public ISlicingListener
{
public:
    virtual void on_sla_object_changed(const Domain::SlicingId&, Sla::Object&&) = 0;
    virtual void on_remove_bed(const Domain::SlicingId&)                        = 0;
};

class IGeneratedSupportPointsListener : public ISlicingListener
{
public:
    virtual void on_generated_support_points_changed(
        GeneratedSupportPointsSnapshot&&,
        const Domain::SlicingId
    ) = 0;
};

struct IStatusListener : ISlicingListener
{
    virtual void on_status_changed(const StatusUpdate, const Domain::SlicingId) = 0;
};

struct IWipeTowerGeometryListener : ISlicingListener
{
    virtual void on_wipe_tower_geometry_changed(Slicing::OptWipeTowerGeometry, const Domain::SlicingId) = 0;
};

struct IExtruderCandidatesListener : ISlicingListener
{
    virtual void on_extruder_candidates_changed(std::vector<unsigned>, const Domain::SlicingId) = 0;
};

struct UpdateRequest
{
    std::reference_wrapper<Domain::Model> model;
    Domain::ProjectMetadata project_metadata;
    Domain::Preset::SelectedPresetMetadata preset_metadata;
    Domain::ConfigPack config;
    std::reference_wrapper<const Domain::BedInstance> bed;
};

struct SlicingRequest
{
    Domain::SlicingId id;
    std::optional<SliceUntilStep> slice_until_step = std::nullopt;
};

/** @brief This is a fatal exception and the backend might be in invalid state after it.
 *  It is expected that the program will gracefully terminate! */
struct FatalSlicingError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

class SlicingInteractor :
    public ISelectedProjectChangedListener,
    public IProcessCallbacks,
    public WithListeners<IStatusListener, IWipeTowerGeometryListener, IExtruderCandidatesListener>,
    public WithListener<
        IFDMResultListener,
        ISLAResultListener,
        ISLAObjectListener,
        IGeneratedSupportPointsListener>
{
public:
    SlicingInteractor(
        Platform::IMainThreadDispatcher& dispatcher,
        IThumbnailImageGenerator& thumbnail_image_generator
    );
    ~SlicingInteractor();

    /* WARNING!
     * Update won't be performed immediately if the background process is running. Rather, the
     * process will be signaled to stop and the update scheduled after the stop happens. This means
     * that model and config references must be valid as long as the process exists! After the process
     * is removed (using remove_process) the validity of the references is no longer required.*/
    void update_process(
        Domain::Model& model,
        const Domain::ProjectMetadata& project_metadata,
        const Domain::Preset::SelectedPresetMetadata& preset_metadata,
        const Domain::ConfigPack& config,
        const Domain::BedInstance& bed
    );

    /* Blocks the UI thread if the process is running! */
    void remove_bed(const Domain::SelectionId bed_instance_id);
    void slice_bed(
        const Domain::SlicingId slicing_id,
        std::optional<SliceUntilStep> slice_until_step = std::nullopt
    );
    void stop_slicing_bed(const Domain::SlicingId slicing_id);
    void slice_all();
    void stop_all();
    void enable_auto_slicing(Domain::SlicingId slicing_id);
    void disable_auto_slicing();

    void on_selected_project_changed(size_t index) override;

    void on_fdm_result(FDMResult&&, Domain::SlicingId) override;
    void on_sla_result(const Domain::SlicingId&, SLAResult&&) override;
    void on_sla_object(const Domain::SlicingId&, Sla::Object&&) override;
    void on_status(const StatusUpdate, Domain::SlicingId) override;
    void on_exception(std::exception_ptr exception, Domain::SlicingId) override;
    void on_wipe_tower_geometry(Slicing::OptWipeTowerGeometry&& wipe_tower_geometry, const Domain::SlicingId id) override;
    void on_extruder_candidates(std::vector<unsigned>&& extruder_candidates, const Domain::SlicingId id) override;
    void on_generated_support_points(
        GeneratedSupportPointsSnapshot&& generated_support_points,
        const Domain::SlicingId id
    ) override;
    StatusCode get_status(const Domain::SlicingId id) const override;

private:
    Domain::SlicingId get_process_id(const Domain::SelectionId bed_instance_id) const;

    void process_slicing_queue();
    void process_update_requests();
    int64_t get_active_processes_count() const;
    void update_status(const Domain::SlicingId id, const StatusCode status);
    void create_process(
        Domain::Model& model,
        const Domain::ProjectMetadata& project_metadata,
        const Domain::Preset::SelectedPresetMetadata& preset_metadata,
        const Domain::ConfigPack& config,
        const Domain::BedInstance& bed,
        const Domain::SlicingId id
    );

    // WARNING: Do not reorder, if you do not know what you are doing!
    // Any members accessed by the threads must be destroyed after
    // the threads!
    std::optional<Domain::SlicingId> m_autoslicing_id;
    mutable std::mutex m_status_mutex;
    std::map<Domain::SlicingId, StatusCode> m_statuses;

    std::deque<SlicingRequest> m_slicing_queue;
    std::map<Domain::SlicingId, UpdateRequest> m_update_requests;
    Domain::SelectionId m_current_project_id{Domain::INVALID_ID};
    Platform::IMainThreadDispatcher& m_dispatcher;
    IThumbnailImageGenerator& m_thumbnail_image_generator;

    // Keep the threads last to be destroyed first.
    std::map<Domain::SlicingId, BackgroundProcess> m_processes;
};

} // namespace Slic3r::Biz::Slicing
