#pragma once

#include <Slic3r/Biz/PrintHost/IPrintHost.hpp>

namespace boost::filesystem { class path; }

namespace Slic3r::Biz::PrintHost {

class PrintHostMKS : public IPrintHost {

public:
    PrintHostMKS(PhysicalPrinter::PhysicalPrinterConfig config, PrintHostJobData data) : IPrintHost(std::move(config), std::move(data)), m_console_port{"8080"} {}
    
    PrintHostMKS(const PrintHostMKS&) = delete;
    PrintHostMKS& operator=(const PrintHostMKS&) = delete;
    PrintHostMKS(PrintHostMKS&& other) noexcept = default;
    PrintHostMKS& operator=(PrintHostMKS&& other) noexcept = default;
   
    ~PrintHostMKS() override {}

    
    bool perform(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const override;

    const char* get_name() const override { return "MKS"; }
    bool test(std::string& msg, RetryFn retry_fn) const override;
private:
    std::string m_console_port;
    std::string get_upload_url(const std::string& filename) const;
    bool start_print(std::string& msg, const std::string& filename) const;

};
} // namespace Slic3r::Biz::PrintHost