#pragma once 

#include <Slic3r/Biz/PrintHost/IPrintHost.hpp>

namespace boost::filesystem { class path; }

namespace Slic3r::Biz::PrintHost {

class PrintHostLocal : public IPrintHost {

public:
    PrintHostLocal(PhysicalPrinter::PhysicalPrinterConfig config, PrintHostJobData data) : IPrintHost(std::move(config), std::move(data)) {}
    
    PrintHostLocal(const PrintHostLocal&) = delete;
    PrintHostLocal& operator=(const PrintHostLocal&) = delete;
    PrintHostLocal(PrintHostLocal&& other) noexcept = default;
    PrintHostLocal& operator=(PrintHostLocal&& other) noexcept = default;
   
    ~PrintHostLocal() override {}

    
    bool perform(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const override;

    const char* get_name() const override { return "Local Export"; }
    bool test(std::string& msg, RetryFn retry_fn) const override { return true; }

    std::string operation_type() const override { return "export"; }
private:
    bool move_file(const boost::filesystem::path& source, const boost::filesystem::path& dest, std::string& msg) const;
};
} // namespace Slic3r::Biz::PrintHost