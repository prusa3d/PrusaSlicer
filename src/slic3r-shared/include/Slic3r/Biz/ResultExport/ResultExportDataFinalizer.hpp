#pragma once

#include "Slic3r/Biz/ResultExport/IResultExportBinarizeListener.hpp"
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"

#include <mutex>

namespace Slic3r::Biz::ResultExport {

class ResultExportDataFinalizer : public WithListeners<IResultExportBinarizeListener>
{
public:
    ResultExportDataFinalizer(Platform::IMainThreadDispatcher& dispatcher);
    ~ResultExportDataFinalizer();

    /**
     * @brief In worker thread takes DataPtrVariant in PrintHostJobData, stores it in a temporary file which path is stored in PrintHostJobData::source_path.
     * On success, moves config and data further.
     */
    void finalize(PhysicalPrinter::PhysicalPrinterConfig&& config, PrintHost::PrintHostJobData&& data);
private:
    mutable std::mutex m_dispatcher_mutex;
    Platform::IMainThreadDispatcher &m_dispatcher;

    void dispatch_success(PhysicalPrinter::PhysicalPrinterConfig config, PrintHost::PrintHostJobData data);
    void dispatch_fail(const std::string& message);
};

}

