#pragma once

#include "Slic3r/Biz/Network/IHttp.hpp"
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"
#include "Slic3r/Biz/PrintHost/PrintHostJobData.hpp"
#include "Slic3r/Biz/PrintHost/PrintHostJobInfoTag.hpp"
#include "Slic3r/Assert.hpp"

#include <string>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>

namespace Slic3r::Biz::PrintHost {

class IPrintHost
{
public:
    explicit IPrintHost(PhysicalPrinter::PhysicalPrinterConfig config, PrintHostJobData data) :
        m_print_host_config(std::move(config)),
        m_upload_data(std::move(data))
    {}

    IPrintHost()                                       = delete;
    IPrintHost(const IPrintHost&)                      = delete;
    IPrintHost& operator=(const IPrintHost&)           = delete;
    IPrintHost(IPrintHost&& other) noexcept            = default;
    IPrintHost& operator=(IPrintHost&& other) noexcept = default;

    virtual ~IPrintHost() {}

    std::string get_host() const
    {
        return m_print_host_config.host;
    }

    typedef Network::IHttp::ProgressFn ProgressFn;
    typedef Network::IHttp::RetryFn RetryFn;
    typedef std::function<void(std::string /* error */)> ErrorFn;
    typedef std::function<void(PrintHostJobInfoTag /* tag */, std::string /* status */)> InfoFn;

    /**
     * Delivers data to given path on host specified in config.
     */
    virtual bool perform(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const = 0;

    virtual const char* get_name() const = 0;
    /**
     * Tests connection to remote host. Most PrintHosts uses test during perform as well.
     */
    virtual bool test(std::string& msg, RetryFn retry_fn) const = 0;

    const PrintHostJobData& upload_data() const
    {
        return m_upload_data;
    }

    // "upload" - PrusaLink, Octoprint etc.
    // "export" - Export to memory
    // "storage" - Storage query for PrusaLink
    virtual std::string operation_type() const { return "upload"; }

protected:
    PhysicalPrinter::PhysicalPrinterConfig m_print_host_config;
    PrintHostJobData m_upload_data;

    virtual std::string format_error(const std::string& body, const std::string& error, unsigned status) const
    {
        if (status != 0) {
            return (boost::format("HTTP %1%: %2%") % status % body).str();
        } else {
            return error;
        }
    }
};

} // namespace Slic3r::Biz::PrintHost
