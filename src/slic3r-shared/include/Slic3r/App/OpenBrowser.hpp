#pragma once

#include <string>

namespace Slic3r::App {

struct OpenBrowserParams
{
    std::string url;
    /// Opens regardless of the hyperlink preference and without the confirmation dialog.
    /// Reserved for flows the user already initiated and that would break otherwise, such as sign-in.
    bool skip_confirmation{false};
    bool is_localized_url{false};
};

/// Opens the URL, honouring the "Open hyperlinks in web browser" preference and its confirmation dialog.
void open_browser(OpenBrowserParams params);

/// False when the "Open hyperlinks in web browser" preference is set to never open; UI elements whose
/// only purpose is opening a link should be disabled then.
bool hyperlinks_allowed();

} // namespace Slic3r::App
