#pragma once

#include <Slic3r/Biz/PrintHost/IPrintHost.hpp>

#include <boost/optional.hpp>

namespace boost::filesystem { class path; }
namespace boost::asio::ip { class address; }

namespace Slic3r::Biz::PrintHost {

class PrintHostOctoPrint : public IPrintHost {

public:
    PrintHostOctoPrint(PhysicalPrinter::PhysicalPrinterConfig config, PrintHostJobData data);
    
    PrintHostOctoPrint(const PrintHostOctoPrint&) = delete;
    PrintHostOctoPrint& operator=(const PrintHostOctoPrint&) = delete;
    PrintHostOctoPrint(PrintHostOctoPrint&& other) noexcept = default;
    PrintHostOctoPrint& operator=(PrintHostOctoPrint&& other) noexcept = default;
   
    ~PrintHostOctoPrint() override = default;


    bool perform(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const override;

    const char* get_name() const override { return "OctoPrint"; }
    bool test(std::string& msg, RetryFn retry_fn) const override;

private:
    bool upload_inner_with_host(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const;
#ifdef WIN32
    bool upload_inner_with_resolved_ip(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn, const boost::asio::ip::address& resolved_addr) const;
    bool test_with_resolved_ip(std::string& msg, RetryFn retry_fn) const;
#endif
    std::string make_url(const std::string& path) const;
    void set_auth(Network::IHttp* http) const;
    bool validate_version_text(const boost::optional<std::string>& version_text) const;
};

} // namespace Slic3r::Biz::PrintHost