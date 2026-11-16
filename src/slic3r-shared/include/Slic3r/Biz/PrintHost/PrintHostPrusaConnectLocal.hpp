#pragma once

#include <Slic3r/Biz/PrintHost/IPrintHost.hpp>

#include <string>
#include <utility>

namespace Slic3r::Biz::PrintHost {

/**
 * @brief Upload to Prusa Connect Local through the legacy team uploads API.
 *
 * The upload is a sequence of requests to the host of the config:
 * 1. POST /app/users/teams/{team_id}/uploads registers the file and returns the upload id.
 * 2. PUT /app/teams/{team_id}/files/raw?upload_id={id} streams the file.
 * .
 */
class PrintHostPrusaConnectLocal : public IPrintHost
{
public:
    PrintHostPrusaConnectLocal(PhysicalPrinter::PhysicalPrinterConfig config, PrintHostJobData data) :
        IPrintHost(std::move(config), std::move(data))
    {}

    PrintHostPrusaConnectLocal(const PrintHostPrusaConnectLocal&)            = delete;
    PrintHostPrusaConnectLocal& operator=(const PrintHostPrusaConnectLocal&) = delete;
    PrintHostPrusaConnectLocal(PrintHostPrusaConnectLocal&& other) noexcept  = default;
    PrintHostPrusaConnectLocal& operator=(PrintHostPrusaConnectLocal&& other) noexcept = default;

    ~PrintHostPrusaConnectLocal() override {}

    bool perform(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const override;

    const char* get_name() const override { return "PrusaConnectLocal"; }
    bool test(std::string& msg, ProgressFn progress_fn, RetryFn retry_fn) const override;

private:
    bool register_upload(
        const PhysicalPrinter::ConnectUpload& auth,
        RetryFn retry_fn,
        ErrorFn error_fn,
        std::size_t& upload_id
    ) const;
    bool upload_file(
        const PhysicalPrinter::ConnectUpload& auth,
        std::size_t upload_id,
        ProgressFn progress_fn,
        RetryFn retry_fn,
        ErrorFn error_fn
    ) const;
};

} // namespace Slic3r::Biz::PrintHost
