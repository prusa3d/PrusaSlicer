#include <fmt/ostream.h>
#include <nlohmann/json.hpp>
#include <Slic3r/Biz/Slicing/BackgroundProcess.hpp>
#include <Slic3r/Assert.hpp>
#include "Slic3r/Biz/Config/ConfigLegacy.hpp"
#include "Slic3r/Biz/Config/ConfigSerialize.hpp"
#include "Slic3r/Domain/GCodeMetadata.hpp"
#include "Slic3r/Biz/Config/GCodeMetadataJson.hpp" // IWYU pragma: keep

#include "Slic3r/Utils.hpp"
#include "Slic3r/Version.hpp"
#include "libslic3r/CanceledException.hpp"
#include "Slic3r/Time.hpp"
#include <boost/algorithm/string.hpp>

namespace Slic3r::Biz::Slicing {

using JThread::StopToken;
using JThread::JThread;
using Domain::ConfigPack;

LoggingScopeLock::LoggingScopeLock(std::mutex& mutex, std::string id)
    : m_mutex{mutex}, m_id{std::move(id)}
{
    SPDLOG_TRACE("Lock '{}' locked", m_id);
    m_mutex.lock();
}

LoggingScopeLock::~LoggingScopeLock() {
    SPDLOG_TRACE("Lock '{}' unlocked", m_id);
    m_mutex.unlock();
}

bool is_thread_active(const StatusCode status) {
    return status == StatusCode::Running
        || status == StatusCode::Stopping
        || status == StatusCode::Updating;
}

Domain::GCodeMetadata build_gcode_metadata(
    const Domain::ProjectMetadata& project_metadata,
    const Domain::Preset::SelectedPresetMetadata& preset_metadata,
    const ConfigPack& config
)
{
    Domain::GCodeMetadata metadata{
        .general =
            {
                .producer         = SLIC3R_APP_NAME,
                .producer_version = SLIC3R_VERSION,
                .time             = Utils::iso_ext_utc_timestamp(),
            },
        .config =
            {.printer = preset_metadata.hw_config,
             .presets =
                 {
                     .vendor    = preset_metadata.hw_config.vendor_id,
                     .repo_id   = preset_metadata.hw_config.repo_id,
                     .version   = preset_metadata.hw_config.repo_version,
                     .printer   = preset_metadata.printer,
                     .print     = preset_metadata.print,
                     .tools     = preset_metadata.tools,
                     .materials = preset_metadata.materials,
                 }},
        .presets = config,
        .project = project_metadata,
        .stats   = {},
    };
    return metadata;
}

IPrint::MetadataSerializeFn build_metadata_serializer(
    const Domain::GCodeMetadata& metadata,
    const Domain::Preset::SelectedPresetMetadata& preset_metadata,
    const ConfigPack& config
)
{
    return [=](const IPrint::UniversalPrintStatistics& gcode_stats) -> SerializedConfig
    {
        // TODO: update captured `metadata.stats` with `gcode_stats`

        const SerializedConfig serialized_config{
            .json = beautify_json(metadata, 2, 14),
            .ini  = Biz::serialize_as_legacy_config(config, preset_metadata.hw_config)
        };

        return serialized_config;
    };
}

BackgroundProcess::BackgroundProcess(
    IProcessCallbacks& callbacks,
    Domain::Model& model,
    const Domain::ProjectMetadata& project_metadata,
    const Domain::Preset::SelectedPresetMetadata& preset_metadata,
    const ConfigPack& config,
    const Domain::BedInstance& bed,
    const Domain::SlicingId id
) :
    m_hw_config_id{preset_metadata.hw_config.id},
    m_print{init_print(preset_metadata.hw_config.technology, callbacks, id)},
    m_on_status{[call = std::reference_wrapper(callbacks), id](const StatusUpdate status) {
        call.get().on_status(status, id);
    }},
    m_on_exception{[call = std::reference_wrapper(callbacks), id](std::exception_ptr exception) {
        call.get().on_exception(exception, id);
    }},
    m_get_status{[call = std::reference_wrapper(callbacks), id]() {
        return call.get().get_status(id);
    }},
    m_id{id}
{
    this->update(model, project_metadata, preset_metadata, config, bed);
};

BackgroundProcess::BackgroundProcess(
    std::unique_ptr<IPrint>&& print,
    IProcessCallbacks& callbacks,
    Domain::Model& model,
    const Domain::ProjectMetadata& project_metadata,
    const Domain::Preset::SelectedPresetMetadata& preset_metadata,
    const ConfigPack& config,
    const Domain::BedInstance& bed,
    const Domain::SlicingId id
)
    : m_hw_config_id{preset_metadata.hw_config.id}
    , m_print{std::move(print)}
    , m_on_status{[call = std::reference_wrapper(callbacks), id](const StatusUpdate status) {call.get().on_status(status, id); }}
    , m_get_status{[call = std::reference_wrapper(callbacks), id]() { return call.get().get_status(id); }}
    , m_id{id}
{
    this->update(model, project_metadata, preset_metadata, config, bed);
};

BackgroundProcess::~BackgroundProcess() {
    // Stop threads before sending status.
    this->m_helper_thread = {};
    this->m_thread = {};

    m_on_status({StatusCode::Removed});
}

void BackgroundProcess::update(
    Domain::Model& model,
    const Domain::ProjectMetadata& project_metadata,
    const Domain::Preset::SelectedPresetMetadata& preset_metadata,
    const ConfigPack& config,
    const Domain::BedInstance& bed
)
{
    SPDLOG_TRACE("{}: update", fmt::streamed(m_id));
    ASSERT(preset_metadata.hw_config.id == m_hw_config_id);

    const LoggingScopeLock lock{m_mutex, "background process"};

    const StatusCode previous_status{m_get_status()};
    ASSERT(!is_thread_active(previous_status), "Update must be called on stopped thread!");
    std::optional<ApplyStatus::Status> apply_status;
    const ScopeGuard guard{[this, &previous_status, &apply_status]() {
        StatusUpdate status_update;
        status_update.clear_progress = true;
        status_update.clear_warnings = true;
        status_update.clear_errors = true;

        // Status is not set, this means that update threw an exception and we are failing.
        if (!apply_status) {
            status_update.code = StatusCode::InvalidData;
            status_update.errors_to_append = {Error()};
            return;
        }

        const bool invalid_data{std::holds_alternative<ApplyStatus::InvalidData>(*apply_status)};
        const bool unchanged{std::holds_alternative<ApplyStatus::Unchanged>(*apply_status)};
        const bool changed{std::holds_alternative<ApplyStatus::Changed>(*apply_status)};
        const bool empty{std::holds_alternative<ApplyStatus::Empty>(*apply_status)};

        if (invalid_data) {
            const auto& invalid_data_status{std::get<ApplyStatus::InvalidData>(*apply_status)};
            status_update.code = StatusCode::InvalidData;
            status_update.errors_to_append = invalid_data_status.errors;
        } else if (empty) {
            status_update.code = StatusCode::Empty;
        } else if (unchanged) {
            status_update.code = previous_status;
            status_update.clear_warnings = false;
            status_update.clear_errors = false;
        } else if (changed){
            const auto& changed_status{std::get<ApplyStatus::Changed>(*apply_status)};
            status_update.code = StatusCode::Modified;
            status_update.warnings_to_append = changed_status.warrnings;
        }  else {
            PANIC("Unreachable state!");
        }
        this->m_on_status(status_update);
    }};

    this->m_on_status({StatusCode::Updating});

    Domain::GCodeMetadata metadata{build_gcode_metadata(project_metadata, preset_metadata, config)};

    apply_status = this->m_print->update(
        model,
        config,
        bed,
        preset_metadata,
        build_metadata_serializer(metadata, preset_metadata, config)
    );
}

void BackgroundProcess::slice(
    IThumbnailImageGenerator& thumbnail_generator,
    const std::optional<SliceUntilStep> slice_until_step
)
{
    SPDLOG_INFO("{}: slice", fmt::streamed(m_id));

    this->stop();
    this->queue_action([this, &thumbnail_generator, slice_until_step]() {
        this->m_thread = {}; // Wait for join.
        ASSERT(!is_thread_active(m_get_status()), "The thread is stopped afterwards!");

        const LoggingScopeLock lock{m_mutex, "background process"};

        if (m_get_status() != StatusCode::Modified) {
            return;
        }
        StatusUpdate status_update;
        status_update.code = StatusCode::Running;
        status_update.progress = Biz::Slicing::Progress{
            Domain::Percentage{0},
            Biz::Slicing::ProgressInfo::Initializing
        };
        m_on_status(status_update);
        this->m_thread = JThread{
            [this, &thumbnail_generator, slice_until_step](StopToken stop_token, IPrint* print) {
                print->stop_token = stop_token;
                print->progress_callback = [this](Biz::Slicing::Progress progress){
                    StatusUpdate status_update;
                    status_update.progress = progress;
                    m_on_status(status_update);
                };
                print->append_warning_callback = [this](const Biz::Slicing::Warning& warning) {
                    StatusUpdate status_update;
                    status_update.warnings_to_append = {warning};
                    m_on_status(status_update);
                };

                bool finished{false};
                std::optional<Biz::Slicing::Error> slicing_error;
                const ScopeGuard guard{[this, slice_until_step, &finished, &slicing_error]() {

                    StatusUpdate status_update;
                    status_update.clear_progress = true;
                    if (finished && !slice_until_step.has_value()) {
                        status_update.code = StatusCode::Finished;
                    } else if(slicing_error) {
                        status_update.code = StatusCode::InvalidData;
                        status_update.errors_to_append = {*slicing_error};
                    } else {
                        // A partially sliced bed still needs the full slicing, so it stays in the Modified state.
                        status_update.code = StatusCode::Modified;
                    }
                    m_on_status(status_update);
                }};

                try {
                    print->slice(m_id, thumbnail_generator, slice_until_step);
                    finished = true;
                } catch (const Exception& exception) {
                    slicing_error = exception.error();
                } catch (CanceledException&) {
                    /* Intentionally pass. */
                } catch (...) {
                    SPDLOG_CRITICAL("Unhandled exception on background thread!");
                    m_on_exception(std::current_exception());
                }
            },
            this->m_print.get()
        };
    });
}

void BackgroundProcess::stop()
{
    this->queue_action([this]() {
        if (m_get_status() == StatusCode::Running) {
            SPDLOG_INFO("{}: stop", fmt::streamed(m_id));
            this->m_thread.request_stop();
            this->m_on_status({StatusCode::Stopping});
        }
    });
}

std::string BackgroundProcess::get_hw_printer_id() const {
    return m_hw_config_id;
}

void BackgroundProcess::queue_action(const std::function<void()>& action)
{
    this->m_helper_thread = {}; // Wait for previous action to finish.
    this->m_helper_thread = JThread{action};
}

} // namespace Slic3r::Biz::BSP
