
#include <Slic3r/Biz/Slicing/SlicingInteractor.hpp>
#include <Slic3r/Biz/Platform/PlatformServices.hpp>
#include "Slic3r/Assert.hpp"
#include "libslic3r/SLAResult.hpp"
#include "libslic3r/I18N.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <fmt/core.h>
#include <fmt/ostream.h>
#include <tracy/Tracy.hpp>

namespace Slic3r::Biz::Slicing {

using Domain::Preset::SelectedPresetMetadata;
using Domain::ProjectMetadata;
using Domain::SlicingId;
using Domain::ConfigPack;

SlicingInteractor::SlicingInteractor(
    Platform::IMainThreadDispatcher& dispatcher,
    IThumbnailImageGenerator& thumbnail_image_generator
) :
    m_dispatcher(dispatcher),
    m_thumbnail_image_generator(thumbnail_image_generator)
{
    Slic3r::I18N_libslic3r::set_translate_callback(Biz::_u8);
}

SlicingInteractor::~SlicingInteractor()
{
    ASSERT(
        m_dispatcher.is_closed(),
        "There must be no queued events (not even in the future),"
        " bacause they may remember the address of this instance!"
    );
}

void SlicingInteractor::create_process(
    Domain::Model& model,
    const ProjectMetadata& project_metadata,
    const SelectedPresetMetadata& preset_metadata,
    const ConfigPack& config,
    const Domain::BedInstance& bed,
    const SlicingId id
)
{
    ZoneScoped;
    SPDLOG_TRACE("{}: create process", fmt::streamed(id));
    update_status(id, StatusCode::Modified);
    m_processes.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(id),
        std::forward_as_tuple(
            *this,
            model,
            project_metadata,
            preset_metadata,
            config,
            bed,
            id
        )
    );
}

void SlicingInteractor::update_process(
    Domain::Model& model,
    const ProjectMetadata& project_metadata,
    const SelectedPresetMetadata& preset_metadata,
    const ConfigPack& config,
    const Domain::BedInstance& bed
)
{
    ZoneScoped;
    const Domain::SelectionId bed_instance_id{bed.id().id};
    const SlicingId id{get_process_id(bed_instance_id)};
    if (m_processes.contains(id)) {
        SPDLOG_TRACE("{}: update process", fmt::streamed(id));

        stop_slicing_bed(id);
        m_update_requests.insert_or_assign(
            id,
            UpdateRequest{model, project_metadata, preset_metadata, config, bed}
        );
        process_update_requests();
        return;
    }

    create_process(model, project_metadata, preset_metadata, config, bed, id);
}

void SlicingInteractor::remove_bed(const Domain::SelectionId bed_instance_id)
{
    const SlicingId id{get_process_id(bed_instance_id)};

    if (id == m_autoslicing_id) {
        m_autoslicing_id = std::nullopt;
    }

    stop_slicing_bed(id);
    m_processes.erase(id);
    {
        const LoggingScopeLock lock{m_status_mutex, "slicing statuses"};
        m_statuses.erase(id);
    }
    process_slicing_queue();

    invoke_listener<IFDMResultListener>([&id](auto* listener) {
        listener->on_fdm_result_changed({}, id);
    });
    invoke_listener<ISLAResultListener>([&id](auto* listener) {
        listener->on_sla_result_changed(id, {});
    });
    invoke_listener<ISLAObjectListener>([&id](auto* listener) { listener->on_remove_bed(id); });

    invoke_listeners<IWipeTowerGeometryListener>(
        [&](auto* listener) { listener->on_wipe_tower_geometry_changed(std::nullopt, id); }
    );

    invoke_listener<IGeneratedSupportPointsListener>(
        [&id](auto* listener) { listener->on_generated_support_points_changed({}, id); }
    );
}

void SlicingInteractor::slice_bed(
    const SlicingId id,
    const std::optional<SliceUntilStep> slice_until_step
)
{
    ASSERT(m_processes.contains(id));
    SPDLOG_TRACE("{}: slicing request", fmt::streamed(id));

    m_slicing_queue.push_back(SlicingRequest{id, slice_until_step});

    process_slicing_queue();
}

void SlicingInteractor::stop_slicing_bed(const Domain::SlicingId id)
{
    if (!m_processes.contains(id)) {
        return;
    }

    const std::size_t removed_request_count{
        std::erase_if(m_slicing_queue, [&id](const SlicingRequest& request) { return request.id == id; })
    };
    if (removed_request_count > 0) {
        SPDLOG_TRACE("{}: slicing_queue: remove", fmt::streamed(id));
    }

    m_processes.at(id).stop();
}

void SlicingInteractor::slice_all()
{
    m_slicing_queue = {};
    for (const auto& pair : m_processes) {
        const SlicingId id{pair.first};
        const StatusCode status{get_status(id)};
        if (status == StatusCode::Empty || status == StatusCode::Finished) {
            continue;
        }
        SPDLOG_TRACE("{}: slicing request", fmt::streamed(id));
        m_slicing_queue.push_back(SlicingRequest{id});
    }
    process_slicing_queue();
}

void SlicingInteractor::stop_all()
{
    m_slicing_queue = {};
    for (auto& pair : m_processes) {
        BackgroundProcess& process{pair.second};
        process.stop();
    }
}

void SlicingInteractor::enable_auto_slicing(Domain::SlicingId slicing_id) {
    m_autoslicing_id = slicing_id;
    process_slicing_queue();
}
void SlicingInteractor::disable_auto_slicing() {
    if (!m_autoslicing_id) {
        return;
    }
    m_autoslicing_id = std::nullopt;
}

void SlicingInteractor::on_selected_project_changed(size_t index)
{
    m_current_project_id = index;
}

SlicingId SlicingInteractor::get_process_id(const Domain::SelectionId bed_instance_id) const
{
    ASSERT(m_current_project_id != Domain::INVALID_ID);
    return {m_current_project_id, bed_instance_id};
}

void SlicingInteractor::on_status(const StatusUpdate status_update, const SlicingId id)
{
    const LoggingScopeLock lock{m_status_mutex, "slicing statuses"};

    SPDLOG_TRACE("{}: status_update: {}", fmt::streamed(id), fmt::streamed(status_update));

    if (!m_statuses.contains(id)) {
        return;
    }

    const bool status_code_udpated{status_update.code && status_update.code != m_statuses.at(id)};
    if (status_update.code) {
        m_statuses.at(id) = *status_update.code;
    }

    if (status_update.code == StatusCode::InvalidData) {
        on_fdm_result({}, id);
        on_sla_result(id, {});
    }

    if (!m_dispatcher.dispatch_on_main_thread([this, status_update, id, status_code_udpated]() {
        if (status_code_udpated) {
            process_slicing_queue();
        }
        invoke_listeners<IStatusListener>([&](auto* listener) {
            listener->on_status_changed(status_update, id);
        });
    }))
    {
        SPDLOG_TRACE("{}: status update dispatched", fmt::streamed(id), fmt::streamed(status_update));
    }
}

void SlicingInteractor::on_exception(std::exception_ptr exception, Domain::SlicingId id) {
    SPDLOG_ERROR("{}: unhandled exception", fmt::streamed(id));
    if (!m_dispatcher.dispatch_on_main_thread([exception, id]() {
            // If possible, obtain the message from the exception.
            try {
                std::rethrow_exception(exception);
            } catch (const std::exception& exception) {
                std::throw_with_nested(
                    FatalSlicingError{fmt::format(
                        "Slicing with id: {} raised unhandled exception: {}",
                        fmt::streamed(id),
                        exception.what()
                    )}
                );
            } catch (...) {
                std::throw_with_nested(
                    FatalSlicingError{fmt::format(
                        "Slicing with id: {} raised unknown unhandled exception!",
                        fmt::streamed(id)
                    )}
                );
            }
        }))
    {
        SPDLOG_TRACE("{}: exception not dispatched", fmt::streamed(id));
    }
}

void SlicingInteractor::on_fdm_result(FDMResult&& result, const SlicingId id)
{
    SPDLOG_TRACE("{}: FDMResult{{moves_count: {}}}", fmt::streamed(id), result.const_moves()->size());

    using Platform::MoveOnlyFunction;

    if (!m_dispatcher.dispatch_on_main_thread([this, id, _result = std::move(result)]() mutable {
        bool already_called{false};
        invoke_listener<IFDMResultListener>([&](auto* listener) {
            listener->on_fdm_result_changed(std::move(_result), id);
            already_called = true;
        });
    }))
    {
        SPDLOG_TRACE("{}: fdm result not dispatched", fmt::streamed(id), result.const_moves()->size());
    }
}

void SlicingInteractor::on_sla_result(const SlicingId& id, SLAResult&& result)
{
    SPDLOG_TRACE("{}: SLAResult{{}}", fmt::streamed(id));
    auto changed = [id, _result = std::move(result)](auto* listener) mutable {
        listener->on_sla_result_changed(id, std::move(_result));
    };
    auto invoke = [this, _changed = std::move(changed)]() mutable {
        invoke_listener<ISLAResultListener>(std::move(_changed));
    };
    if (!m_dispatcher.dispatch_on_main_thread(std::move(invoke))) {
        SPDLOG_TRACE("{}: sla result not dispatched", fmt::streamed(id));
    }
}

void SlicingInteractor::on_sla_object(const SlicingId& id, Sla::Object&& instance)
{
    SPDLOG_TRACE("{}: SLAInstance{{}}", fmt::streamed(id));
    auto changed = [id, _instance = std::move(instance)](auto* listener) mutable {
        listener->on_sla_object_changed(id, std::move(_instance));
    };
    auto invoke = [this, _changed = std::move(changed)]() mutable {
        invoke_listener<ISLAObjectListener>(std::move(_changed));
    };
    if (!m_dispatcher.dispatch_on_main_thread(std::move(invoke))) {
        SPDLOG_TRACE("{}: sla instance not dispatched", fmt::streamed(id));
    }
}

void SlicingInteractor::on_wipe_tower_geometry(
    Slicing::OptWipeTowerGeometry&& wipe_tower_geometry,
    const SlicingId id
)
{
    if (wipe_tower_geometry) {
        SPDLOG_TRACE("{}: OptWipeTowerGeometry{{depths.size: {}}}", fmt::streamed(id), wipe_tower_geometry->depths.size());
    } else {
        SPDLOG_TRACE("{}: OptWipeTowerGeometry{{nullopt}}", fmt::streamed(id));
    }

    if (!m_dispatcher.dispatch_on_main_thread(
            [this, id, geometry = std::move(wipe_tower_geometry)]() mutable {
        invoke_listeners<IWipeTowerGeometryListener>([&](auto* listener) {
            listener->on_wipe_tower_geometry_changed(geometry, id);
        });
    }
        ))
    {
        SPDLOG_TRACE("{}: wipe tower geometry not dispatched", fmt::streamed(id));
    }
}

void SlicingInteractor::on_extruder_candidates(
    std::vector<unsigned>&& extruder_candidates,
    const Domain::SlicingId id
)
{
    SPDLOG_TRACE("{}: extruder_candidates", fmt::streamed(id));
    if (!m_dispatcher.dispatch_on_main_thread(
            [this, id, candidates = std::move(extruder_candidates)]() mutable {
        invoke_listeners<IExtruderCandidatesListener>([&](auto* listener) {
            listener->on_extruder_candidates_changed(candidates, id);
        });
    }
        ))
    {
        SPDLOG_TRACE("{}: extruder_candidates not dispatched", fmt::streamed(id));
    }
}

void SlicingInteractor::on_generated_support_points(
    GeneratedSupportPointsSnapshot&& generated_support_points,
    const SlicingId id
)
{
    SPDLOG_TRACE(
        "{}: GeneratedSupportPointsSnapshot{{objects: {}}}",
        fmt::streamed(id),
        generated_support_points.size()
    );

    if (!m_dispatcher.dispatch_on_main_thread(
            [this, id, support_points = std::move(generated_support_points)]() mutable
            {
                invoke_listener<IGeneratedSupportPointsListener>(
                    [&](auto* listener)
                    {
                        listener->on_generated_support_points_changed(
                            std::move(support_points),
                            id
                        );
                    }
                );
            }
        ))
    {
        SPDLOG_TRACE("{}: generated support points not dispatched", fmt::streamed(id));
    }
}

void SlicingInteractor::process_slicing_queue()
{
    process_update_requests();

    if (m_autoslicing_id && m_processes.contains(*m_autoslicing_id)) {
        const LoggingScopeLock lock{m_status_mutex, "slicing statuses"};
        if (m_statuses.at(*m_autoslicing_id) == Slicing::StatusCode::Modified) {
            const auto it{
                std::ranges::find(m_slicing_queue, *m_autoslicing_id, &SlicingRequest::id)
            };
            if (it == m_slicing_queue.end()) {
                m_slicing_queue.push_front(SlicingRequest{*m_autoslicing_id});
            }
        }
    }

    if (m_slicing_queue.empty()) {
        return;
    }

    if (get_active_processes_count() >= 2) {
        return;
    }

    SPDLOG_TRACE("slicing_queue: {}", m_slicing_queue.size());

    const SlicingRequest to_slice{m_slicing_queue.front()};
    m_slicing_queue.pop_front();

    if (!m_processes.contains(to_slice.id)) {
        SPDLOG_TRACE("{}: removed: nonexistent", fmt::streamed(to_slice.id));
        return;
    }

    m_processes.at(to_slice.id).slice(m_thumbnail_image_generator, to_slice.slice_until_step);
}

void SlicingInteractor::process_update_requests()
{
    ZoneScoped;

    if (m_update_requests.empty()) {
        return;
    }

    std::erase_if(m_update_requests, [&](const auto& pair) {
        const SlicingId id{pair.first};
        return !m_processes.contains(id);
    });

    SPDLOG_TRACE("update_requests: {}", m_update_requests.size());

    std::set<SlicingId> to_remove;
    for (auto& [id, request] : m_update_requests) {
        BackgroundProcess& process{m_processes.at(id)};
        if (is_thread_active(get_status(id))) {
            continue;
        }

        if (process.get_hw_printer_id() != request.preset_metadata.hw_config.id) {
            m_processes.erase(id);
            create_process(
                request.model.get(),
                request.project_metadata,
                request.preset_metadata,
                request.config,
                request.bed.get(),
                id
            );
        } else {
            process.update(
                request.model.get(),
                request.project_metadata,
                request.preset_metadata,
                request.config,
                request.bed.get()
            );
        }
        to_remove.insert(id);
    }

    std::erase_if(m_update_requests, [&](const auto& pair) {
        const SlicingId id{pair.first};
        return to_remove.contains(id);
    });
}

void SlicingInteractor::update_status(const SlicingId id, const StatusCode status)
{
    const LoggingScopeLock lock{m_status_mutex, "slicing statuses"};
    m_statuses[id] = status;
}

int64_t SlicingInteractor::get_active_processes_count() const
{
    const LoggingScopeLock lock{m_status_mutex, "slicing statuses"};
    return std::ranges::count_if(m_statuses, [](const auto& pair) {
        const StatusCode status{pair.second};
        return is_thread_active(status);
    });
}

StatusCode SlicingInteractor::get_status(const SlicingId id) const
{
    const LoggingScopeLock lock{m_status_mutex, "slicing statuses"};
    ASSERT(m_statuses.contains(id));

    return m_statuses.at(id);
}

} // namespace Slic3r::Biz::Slicing
