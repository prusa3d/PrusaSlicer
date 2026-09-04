#include "Slic3r/Biz/PrintHost/PrintHostJobData.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Log.hpp"

namespace Slic3r::Biz::PrintHost {

PrintHostExportFormat get_export_format_from_extension(const std::string& extension)
{
    SPDLOG_INFO("{} extension: {}", __FUNCTION__, extension);
    if (extension == ".gcode") 
        return PrintHostExportFormat::GCode;
    if (extension == ".bgcode") {
        return PrintHostExportFormat::BGCode;
    }
    if (extension == ".sl1") {
        return PrintHostExportFormat::Sl1;
    }
    if (extension == ".sl1s") {
        return PrintHostExportFormat::Sl1s;
    }
    ASSERT(false, "Unknown data format. Add it to PrintHostResultFormat");
    return PrintHostExportFormat::Undefined;
}

std::vector<PrintHostAfterUploadAction> get_post_upload_actions(Domain::PrintHostType type)
{
    switch (type) {
    case Domain::PrintHostType::OctoPrint:
    case Domain::PrintHostType::PrusaLink:
    case Domain::PrintHostType::Moonraker:
    case Domain::PrintHostType::AstroBox:
    case Domain::PrintHostType::Repetier:
    case Domain::PrintHostType::MKS:
        return {PrintHostAfterUploadAction::StartPrint};
    case Domain::PrintHostType::Duet:
        return {PrintHostAfterUploadAction::StartPrint, PrintHostAfterUploadAction::StartSimulation};
    case Domain::PrintHostType::SL1Host:
    case Domain::PrintHostType::FlashAir:
    case Domain::PrintHostType::PrusaLinkStorage:
        return {};
    }
    ASSERT(false, "Unknown print host type. Add it to get_post_upload_actions");
    return {};
}

} // namespace Slic3r::Biz::PrintHost