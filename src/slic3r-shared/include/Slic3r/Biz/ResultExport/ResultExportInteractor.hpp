#pragma once

#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include "Slic3r/Biz/PrintHost/PrintHostJobManager.hpp"
#include "Slic3r/Biz/ResultExport/ResultExportDataFinalizer.hpp"

#include <boost/filesystem.hpp>
#include <map>

namespace Slic3r::Biz::ResultExport {

class PrintHostJobManager;

class ResultExportInteractor : public IResultExportBinarizeListener
{
public:
    ResultExportInteractor(Platform::IMainThreadDispatcher& dispatcher);

    typedef std::function<void(const std::string& storage_json)> StorageInfoFn;

    void on_storage_resolved(size_t id, const std::string& storage);

    /**
     * @brief Accepts data from PrintHostJobManager and passes it to ResultExportDataFinalizer and process_gcode_inner.
     */
    void perform(PhysicalPrinter::PhysicalPrinterConfig config, PrintHost::PrintHostJobData data);

    void on_result_export_binarize_success(PhysicalPrinter::PhysicalPrinterConfig config, PrintHost::PrintHostJobData data) override;
    void on_result_export_binarize_fail(const std::string& msg) override;

private:
    PrintHost::PrintHostJobManager m_print_host_job_manager;
    ResultExportDataFinalizer m_result_export_data_finalizer;
    std::map<size_t, StorageInfoFn> m_storage_callbacks_map;

    /**
     * @brief Passes data to PrintHostJobManager to first start storage resolve job and stages upload job data to m_storage_callbacks_map.
     */
    void upload_gcode_with_storage_choice(PhysicalPrinter::PhysicalPrinterConfig config, PrintHost::PrintHostJobData data);

    /**
     * @brief Passes data to PrintHostJobManager to create and start export / upload job.
     */
    void process_gcode_inner(PhysicalPrinter::PhysicalPrinterConfig config, PrintHost::PrintHostJobData data);
};
} // namespace Slic3r::Biz::PrintHost
