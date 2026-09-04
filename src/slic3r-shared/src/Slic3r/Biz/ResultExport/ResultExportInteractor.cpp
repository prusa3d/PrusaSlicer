#include "Slic3r/Biz/ResultExport/ResultExportInteractor.hpp"

#include "Slic3r/Log.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"

#include "Slic3r/LegacyFormat.hpp"

#include <nlohmann/json.hpp>

namespace fs = boost::filesystem;

namespace Slic3r::Biz::ResultExport {

ResultExportInteractor::ResultExportInteractor(Platform::IMainThreadDispatcher& dispatcher) :
    m_result_export_data_finalizer(dispatcher)
{
    m_result_export_data_finalizer.add_listener<IResultExportBinarizeListener>(this);
}

void ResultExportInteractor::on_storage_resolved(size_t id, const std::string& storage)
{
    if (auto node = m_storage_callbacks_map.extract(id); !node.empty()) {
        node.mapped()(storage);
    }
}

void ResultExportInteractor::perform(PhysicalPrinter::PhysicalPrinterConfig config, PrintHost::PrintHostJobData data)
{
    m_result_export_data_finalizer.finalize(std::move(config), std::move(data));
}

void ResultExportInteractor::process_gcode_inner(PhysicalPrinter::PhysicalPrinterConfig config, PrintHost::PrintHostJobData data)
{
    if (const auto* auth = std::get_if<PhysicalPrinter::PrinterUpload>(&config.payload);
        auth && auth->type == Domain::PrintHostType::PrusaLink)
    {
        upload_gcode_with_storage_choice(std::move(config), std::move(data));
        return;
    }
    size_t id = m_print_host_job_manager.emplace_job(std::move(config), std::move(data));
}

void ResultExportInteractor::upload_gcode_with_storage_choice(PhysicalPrinter::PhysicalPrinterConfig config, PrintHost::PrintHostJobData data)
{
    // Since copy constructor and assign are deleted for PhysicalPrinter::PhysicalPrinterConfig and PrintHostJobData,
    // We use shared pointer to be able to move them to the callback lambda and copy the lambda
    auto config_ptr = std::make_shared<PhysicalPrinter::PhysicalPrinterConfig>(std::move(config));
    auto data_ptr   = std::make_shared<PrintHost::PrintHostJobData>(std::move(data));

    PhysicalPrinter::PrinterUpload* other_auth = std::get_if<PhysicalPrinter::PrinterUpload>(&config_ptr->payload);
    ASSERT(other_auth);

    StorageInfoFn callback = [this, config_ptr, data_ptr](const std::string& storage) mutable
    {
        data_ptr->storage = storage;
        m_print_host_job_manager.emplace_job(std::move(*config_ptr), std::move(*data_ptr));
    };

    PhysicalPrinter::PrinterUpload new_auth = *other_auth;
    new_auth.type = Domain::PrintHostType::PrusaLinkStorage;

    PhysicalPrinter::PhysicalPrinterConfig storage_config{
        .payload = std::move(new_auth),
        .host    = config_ptr->host
    };

    size_t id = m_print_host_job_manager.emplace_job(std::move(storage_config), {std::monostate{}, data_ptr->dest_path, PrintHost::PrintHostExportFormat::Undefined});

    m_storage_callbacks_map[id] = std::move(callback);
}

void ResultExportInteractor::on_result_export_binarize_success(PhysicalPrinter::PhysicalPrinterConfig config, PrintHost::PrintHostJobData data)
{
    process_gcode_inner(std::move(config), std::move(data));
}

void ResultExportInteractor::on_result_export_binarize_fail(const std::string& msg)
{
    SPDLOG_ERROR("ResultExportDataFinalizer has failed: {}", msg);
}

} // namespace Slic3r::Biz::PrintHost
