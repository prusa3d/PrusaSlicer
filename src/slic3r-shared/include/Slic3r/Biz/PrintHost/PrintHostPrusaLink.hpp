#pragma once

#include <Slic3r/Biz/PrintHost/IPrintHost.hpp>

#include <boost/optional.hpp>

namespace boost::filesystem { class path; }
namespace boost::asio::ip { class address; }

namespace Slic3r::Biz::PrintHost {

class PrintHostPrusaLink : public IPrintHost{

public:
    PrintHostPrusaLink(PhysicalPrinter::PhysicalPrinterConfig config, PrintHostJobData data);
    
    PrintHostPrusaLink(const PrintHostPrusaLink&) = delete;
    PrintHostPrusaLink& operator=(const PrintHostPrusaLink&) = delete;
    PrintHostPrusaLink(PrintHostPrusaLink&& other) noexcept = default;
    PrintHostPrusaLink& operator=(PrintHostPrusaLink&& other) noexcept = default;
   
    ~PrintHostPrusaLink() override = default;

    bool perform(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const override;

    const char* get_name() const override { return "PrusaLink"; }
    bool test(std::string& msg, RetryFn retry_fn) const override;

protected:
    virtual void set_http_post_header_args(Network::IHttp* http, PrintHostAfterUploadAction action) const;
     virtual bool validate_version_text(const boost::optional<std::string>& version_text) const;

    void set_auth(Network::IHttp* http) const;
    std::string make_url(const std::string& path) const;
    bool upload_inner_with_host(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const;
    bool put_inner(std::string url, const std::string& name, ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const;
    bool post_inner(std::string url, const std::string& name, ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const;
    bool test_with_method_check(std::string& msg, bool& use_put, RetryFn retry_fn) const;
#ifdef WIN32
    bool upload_inner_with_resolved_ip(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn, const boost::asio::ip::address& resolved_addr) const;
    bool test_with_resolved_ip_and_method_check(std::string& msg, bool& use_put, RetryFn retry_fn) const;
#endif
};

} // namespace Slic3r::Biz::PrintHost