#pragma once

#include <Slic3r/Biz/PrintHost/IPrintHost.hpp>

namespace boost::filesystem { class path; }

namespace Slic3r::Biz::PrintHost {

class PrintHostAstroBox : public IPrintHost {

public:
    PrintHostAstroBox(PhysicalPrinter::PhysicalPrinterConfig config, PrintHostJobData data) : IPrintHost(std::move(config), std::move(data)) {}
    
    PrintHostAstroBox(const PrintHostAstroBox&) = delete;
    PrintHostAstroBox& operator=(const PrintHostAstroBox&) = delete;
    PrintHostAstroBox(PrintHostAstroBox&& other) noexcept = default;
    PrintHostAstroBox& operator=(PrintHostAstroBox&& other) noexcept = default;
   
    ~PrintHostAstroBox() override {}

    
    bool perform(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const override;

    const char* get_name() const override { return "AstroBox"; }
    bool test(std::string& msg, RetryFn retry_fn) const override;
private:
    std::string make_url(const std::string& path) const;
    void set_auth(Network::IHttp* http) const;
    bool validate_version_text(const boost::optional<std::string>& version_text) const;
};
} // namespace Slic3r::Biz::PrintHost