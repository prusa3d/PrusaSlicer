#pragma once

#include <string>

namespace Slic3r::App {

struct OpenBrowserParams
{
    std::string url;
    bool force_remember_choice{true};
    bool is_localized_url{false};
};

/// Opens the URL, honouring the hyperlink suppression preference and its confirmation dialog.
void open_browser(OpenBrowserParams params);

} // namespace Slic3r::App
