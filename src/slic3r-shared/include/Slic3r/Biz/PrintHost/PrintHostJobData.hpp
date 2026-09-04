#pragma once

#include "Slic3r/Assert.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Domain/ConfigPhysical.hpp"
#include <string>
#include <boost/filesystem.hpp>
#include <memory>
#include <variant>
#include <vector>

namespace Slic3r::Biz::libpgcode {
class LineView;
} // namespace Slic3r::Biz::libpgcode

namespace Slic3r::Biz::Slicing {
struct SLAResultData;
}

namespace Slic3r::Biz::PrintHost {

using DataPtrVariant = std::variant<
    std::monostate,
    std::shared_ptr<const libpgcode::LineView>,
    std::shared_ptr<const Slicing::SLAResultData>>;

enum class PrintHostAfterUploadAction
{
    None,
    StartPrint,
    StartSimulation,
    QueuePrint
};

std::vector<PrintHostAfterUploadAction> get_post_upload_actions(Domain::PrintHostType type);

enum class PrintHostExportFormat
{
    Undefined,
    GCode,
    BGCode,
    Sl1,
    Sl1s,
};

PrintHostExportFormat get_export_format_from_extension(const std::string& extension);

struct PrintHostJobData
{
    DataPtrVariant data_ptr;

    boost::filesystem::path source_path;
    boost::filesystem::path dest_path;

    PrintHostAfterUploadAction post_action{PrintHostAfterUploadAction::None};
    std::string storage;
    std::string group;
    std::string request_body_json;

    PrintHostExportFormat result_format;

    PrintHostJobData() = delete;

    PrintHostJobData(DataPtrVariant data, const boost::filesystem::path& dest, PrintHostExportFormat result_format) :
        data_ptr(data),
        dest_path(dest),
        result_format(result_format)
    {}

    PrintHostJobData(PrintHostJobData&& other) noexcept = default;

    PrintHostJobData& operator=(PrintHostJobData&& other) noexcept = default;

    PrintHostJobData(const PrintHostJobData& other)            = delete;
    PrintHostJobData& operator=(const PrintHostJobData& other) = delete;
};


} // namespace Slic3r::Biz::PrintHost
