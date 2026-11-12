#include "Slic3r/Biz/PrintHost/PrintHostJobData.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Log.hpp"

namespace Slic3r::Biz::PrintHost {

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