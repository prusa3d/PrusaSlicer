#pragma once

#include <Slic3r/Biz/PrintHost/IPrintHost.hpp>

namespace boost::filesystem { class path; }

namespace Slic3r::Biz::PrintHost {

class PrintHostFlashAir : public IPrintHost {

public:
    PrintHostFlashAir(PhysicalPrinter::PhysicalPrinterConfig config, PrintHostJobData data) : IPrintHost(std::move(config), std::move(data)) {}
    
    PrintHostFlashAir(const PrintHostFlashAir&) = delete;
    PrintHostFlashAir& operator=(const PrintHostFlashAir&) = delete;
    PrintHostFlashAir(PrintHostFlashAir&& other) noexcept = default;
    PrintHostFlashAir& operator=(PrintHostFlashAir&& other) noexcept = default;
   
    ~PrintHostFlashAir() override {}

    
    bool perform(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const override;

    const char* get_name() const override { return "FlashAir"; }
    bool test(std::string& msg, RetryFn retry_fn) const override;

private:
    std::string make_url(const std::string& path) const;
    std::string make_url(const std::string& path, const std::string& arg, const std::string& val) const;

};
} // namespace Slic3r::Biz::PrintHost