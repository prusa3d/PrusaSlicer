#pragma once

#include <Slic3r/Biz/PrintHost/IPrintHost.hpp>

namespace boost::filesystem { class path; }

namespace Slic3r::Biz::PrintHost {

class PrintHostRepetier : public IPrintHost {

public:
    PrintHostRepetier(PhysicalPrinter::PhysicalPrinterConfig config, PrintHostJobData data) : IPrintHost(std::move(config), std::move(data)) {}
    
    PrintHostRepetier(const PrintHostRepetier&) = delete;
    PrintHostRepetier& operator=(const PrintHostRepetier&) = delete;
    PrintHostRepetier(PrintHostRepetier&& other) noexcept = default;
    PrintHostRepetier& operator=(PrintHostRepetier&& other) noexcept = default;
   
    ~PrintHostRepetier() override {}

    
    bool perform(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const override;

    const char* get_name() const override { return "Repetier"; }
    bool test(std::string& msg, RetryFn retry_fn) const override;

private:
    std::string make_url(const std::string& path) const;
    void set_auth(Network::IHttp* http) const;

};
} // namespace Slic3r::Biz::PrintHost