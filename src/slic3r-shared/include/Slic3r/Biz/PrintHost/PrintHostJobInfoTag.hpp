#pragma once

namespace Slic3r::Biz::PrintHost {

enum class PrintHostJobInfoTag
{
    None, // Default state when PrintHostJobProgressPayload does not carry PrintHostJobInfoTag
    Error,
    Filename,
    Resolve,
    OperationType,
    Storage,
    // Page of the printer in Prusa Connect, the upload went to it.
    ConnectPrinterAddress,
    // Tags used int PS2 implementation (might have to be implemented when Physical printer presets are implemented)
    //complete_with_warning
    //complete
    //set_complete_off
};

}