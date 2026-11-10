#pragma once

#include <Slic3r/Biz/PrintHost/IPrintHost.hpp>

#include <string>
#include <utility>
#include <vector>

namespace Slic3r::Biz::PrintHost {

/**
 * @brief Upload to Prusa Connect through the slicer REST API.
 *
 * The upload is a sequence of requests:
 * 1. POST .../organizations/{org}/files registers the file and returns a presigned upload URL.
 * 2. PUT <upload_url> streams the file to the storage bucket (no Authorization header).
 * 3. PATCH .../organizations/{org}/files/{file_id} marks the upload as finished.
 * 4. POST .../printers/{printer_uuid}/queue hands the file to the printer or its queue.
 * .
 */
class PrintHostPrusaConnect : public IPrintHost
{
public:
    PrintHostPrusaConnect(PhysicalPrinter::PhysicalPrinterConfig config, PrintHostJobData data) :
        IPrintHost(std::move(config), std::move(data))
    {}

    PrintHostPrusaConnect(const PrintHostPrusaConnect&)            = delete;
    PrintHostPrusaConnect& operator=(const PrintHostPrusaConnect&) = delete;
    PrintHostPrusaConnect(PrintHostPrusaConnect&& other) noexcept  = default;
    PrintHostPrusaConnect& operator=(PrintHostPrusaConnect&& other) noexcept = default;

    ~PrintHostPrusaConnect() override {}

    bool perform(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const override;

    const char* get_name() const override { return "PrusaConnect"; }
    bool test(std::string& msg, ProgressFn progress_fn, RetryFn retry_fn) const override;

private:
    struct FileRecord
    {
        std::string upload_url;
        std::string file_id;
        std::vector<std::pair<std::string, std::string>> upload_headers;
    };

    bool create_file_record(
        const PhysicalPrinter::ConnectUpload& auth,
        RetryFn retry_fn,
        ErrorFn error_fn,
        FileRecord& out
    ) const;
    bool upload_to_bucket(
        const FileRecord& record,
        ProgressFn progress_fn,
        RetryFn retry_fn,
        ErrorFn error_fn
    ) const;
    bool finish_file_record(
        const PhysicalPrinter::ConnectUpload& auth,
        const FileRecord& record,
        RetryFn retry_fn,
        ErrorFn error_fn
    ) const;
    bool enqueue_on_printer(
        const PhysicalPrinter::ConnectUpload& auth,
        const FileRecord& record,
        RetryFn retry_fn,
        ErrorFn error_fn
    ) const;
};

} // namespace Slic3r::Biz::PrintHost
