#include "Slic3r/Biz/PrintHost/PrintHostJobManager.hpp"

#include "Slic3r/Biz/PrintHost/PrintHostJob.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/IdGenerator.hpp"

#include "Slic3r/Biz/PrintHost/PrintHostFactory.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"

namespace Slic3r::Biz::PrintHost {

namespace {
static size_t next_id()
{
    static IdGenerator<size_t> id_generator(0);
    return id_generator.next_id();
}
} // namespace

PrintHostJobManager::PrintHostJobManager() {}

PrintHostJobManager::~PrintHostJobManager() = default;

struct PrintHostFailed : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

size_t PrintHostJobManager::emplace_job(PhysicalPrinter::PhysicalPrinterConfig config, PrintHostJobData data)
{
    size_t id      = next_id();
    m_wrappers[id] = std::make_shared<PrintHostJobWrapper>(
        create_print_host(std::move(config), std::move(data)),
        id
    );
    PrintHostJobWrapper& wrapper = *m_wrappers[id].get();
    // Find unfinished jobs with same host. These are dependencies that needs to finish before this job can start.
    for (const auto& [key, value] : m_wrappers) {
        if (key == id) {
            continue;
        }
        if (value->host == wrapper.host) {
            ASSERT(key < id, "We expect id to be the latest value.");
            wrapper.dependencies.push_back(value);
        }
    }

    std::function job_func = [this, &wrapper](Biz::JThread::StopToken stop_token,
                                 Biz::Platform::JobManager::ProgressTracker progress_tracker) -> bool
    {
        for (const auto& dep : wrapper.dependencies) {
            dep->future.wait(); // Wait for dependency to finish
        }

        progress_tracker.set_progress_detail(
            {(int) PrintHostJobProgressState::Info,
             PrintHostJobProgressPayload{
                 wrapper.id,
                 PrintHostJobInfoTag::Filename,
                 wrapper.print_host->upload_data().dest_path.filename().string()
             }}
        );

        progress_tracker.set_progress_detail(
            {(int) PrintHostJobProgressState::Info,
             PrintHostJobProgressPayload{
                 wrapper.id,
                 PrintHostJobInfoTag::OperationType,
                 wrapper.print_host->operation_type()
             }}
        );

        // Clean ProgressDetail
        progress_tracker.set_progress_detail({0, PrintHostJobProgressPayload{wrapper.id}});

        bool success = wrapper.print_host->perform(
            [stop_token, &progress_tracker](Network::IHttp::Progress progress, bool& cancel)
            {
                // this->on_progress_fn(std::move(progress), cancel);
                double prg = (progress.ultotal > 0 ? static_cast<double>(progress.ulnow) / static_cast<double>(progress.ultotal) : 0.0);
                Domain::Percentage perc(prg);
                progress_tracker.set(std::move(perc));
                if (stop_token.stop_requested())
                    cancel = true;
            },
            [stop_token, &progress_tracker](Network::IHttp::Retry retry, bool& cancel)
            {
                if (stop_token.stop_requested())
                    cancel = true;
            },
            [&progress_tracker,&wrapper](std::string error)
            { 
                progress_tracker.set_progress_detail(
                    {(int) PrintHostJobProgressState::Error,
                     PrintHostJobProgressPayload{wrapper.id, PrintHostJobInfoTag::Error, std::move(error)}}
                );
                progress_tracker.set_status(Domain::JobStatus::Failed); },
            [&progress_tracker, &wrapper](PrintHostJobInfoTag tag, std::string message)
            {
                progress_tracker.set_progress_detail(
                    {(int) PrintHostJobProgressState::Info,
                     PrintHostJobProgressPayload{wrapper.id, std::move(tag), std::move(message)}}
                );
                progress_tracker.set_progress_detail({0, PrintHostJobProgressPayload{wrapper.id}});
            }

        );
        wrapper.promise.set_value();
        if (!success) {
            throw PrintHostFailed{""};
        }
        return true;
    };

    Platform::PlatformServices::instance()
        .job_manager()
        .create_job("printhost" + std::to_string(id), std::move(job_func))
        .on_result([id, this](bool result) { 
            // This function is called also when cancel_job was used -> wrappers should be managed correctly.
            m_wrappers.erase(id); 
        })
        .on_exception(
            [id, this](const std::exception_ptr& exception)
            {
                m_wrappers.erase(id);
            }
        )
        .start();
    return id;
}

void PrintHostJobManager::cancel_job(size_t id)
{
    Platform::PlatformServices::instance().job_manager().cancel_job(
        "printhost" + std::to_string(id)
    );
}

} // namespace Slic3r::Biz::PrintHost
