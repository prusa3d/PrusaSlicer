#pragma once

#include <Slic3r/Biz/PrintHost/IPrintHost.hpp>

namespace boost::filesystem { class path; }

namespace Slic3r::Biz::PrintHost {

class PrintHostPrusaConnect : public IPrintHost {

public:
    PrintHostPrusaConnect(PhysicalPrinter::PhysicalPrinterConfig config, PrintHostJobData data) : IPrintHost(std::move(config), std::move(data))
    {}

    PrintHostPrusaConnect(const PrintHostPrusaConnect&) = delete;
    PrintHostPrusaConnect& operator=(const PrintHostPrusaConnect&) = delete;
    PrintHostPrusaConnect(PrintHostPrusaConnect&& other) noexcept = default;
    PrintHostPrusaConnect& operator=(PrintHostPrusaConnect&& other) noexcept = default;
   
    ~PrintHostPrusaConnect() override {}

    
    bool perform(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const override;

    const char* get_name() const override { return "PrusaConnect"; }
    bool test(std::string& msg, RetryFn retry_fn) const override;

private:
    bool init_upload(const PrintHostJobData& upload_data, std::string& out, RetryFn retry_fn) const;
};
} // namespace Slic3r::Biz::PrintHost