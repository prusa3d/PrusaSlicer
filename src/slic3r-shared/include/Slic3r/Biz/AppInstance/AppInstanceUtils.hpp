#pragma once

#include <string>

namespace Slic3r::Biz::AppInstance {
unsigned get_current_pid();

/**
 * @brief Replaces the query string of every prusaslicer:// URL in the text with a placeholder.
 *
 * Login callbacks carry a single-use OAuth authorization code and download callbacks carry
 * user specific model URLs, so no query string may reach a log file. The scheme and path are
 * kept, which is enough to tell which callback arrived.
 */
std::string redact_app_urls(const std::string& text);
} // namespace Slic3r::Biz::AppInstance
