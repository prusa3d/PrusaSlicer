#pragma once

#include "Slic3r/Biz/PrintHost/IPrintHost.hpp"
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"
#include "Slic3r/Biz/PrintHost/PrintHostJobData.hpp"

#include <boost/optional.hpp>


namespace Slic3r::Biz::PrintHost {

class PrintHostPrusaLinkStorage : public IPrintHost {
public:
    PrintHostPrusaLinkStorage(PhysicalPrinter::PhysicalPrinterConfig config, PrintHostJobData data);
    
    PrintHostPrusaLinkStorage(const PrintHostPrusaLinkStorage&) = delete;
    PrintHostPrusaLinkStorage& operator=(const PrintHostPrusaLinkStorage&) = delete;
    PrintHostPrusaLinkStorage(PrintHostPrusaLinkStorage&& other) noexcept = default;
    PrintHostPrusaLinkStorage& operator=(PrintHostPrusaLinkStorage&& other) noexcept = default;
   
    ~PrintHostPrusaLinkStorage() override = default;

    /**
     * Returns retrieved Storage via InfoFn. 
     */
    bool perform(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const override;

    const char* get_name() const override { return "PrusaLink"; }
    bool test(std::string& msg, RetryFn retry_fn) const override {return false; }

    std::string operation_type() const override { return "storage"; }
private:
    std::string make_url(const std::string& path) const;
    bool set_auth(Network::IHttp* http, std::string& err_msg) const;
};

}