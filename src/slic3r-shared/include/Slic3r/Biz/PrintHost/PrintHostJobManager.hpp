#pragma once

#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"
#include "Slic3r/Biz/PrintHost/PrintHostJobData.hpp"
#include "Slic3r/Biz/PrintHost/PrintHostJob.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"

#include <map>
#include <memory>
#include <mutex>

namespace Slic3r::Biz::PrintHost {

class PrintHostJobManager
{
public:
    PrintHostJobManager();
    ~PrintHostJobManager();

    PrintHostJobManager(const PrintHostJobManager &) = delete;
    PrintHostJobManager(PrintHostJobManager &&other) = delete;
    PrintHostJobManager& operator=(const PrintHostJobManager &) = delete;
    PrintHostJobManager& operator=(PrintHostJobManager &&other) = delete;

    /*
     * @brief Defines and starts job in job manager. 
     */
    size_t emplace_job(PhysicalPrinter::PhysicalPrinterConfig config, PrintHostJobData data);

    void cancel_job(size_t id);

private:

    std::map<size_t,std::shared_ptr<PrintHostJobWrapper>> m_wrappers;
};

} // namespace Slic3r::Biz::Slicing
