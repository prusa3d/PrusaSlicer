#pragma once

#include <optional>
#include <string>
#include <vector>

namespace Slic3r::Biz::ConnectPrinter {

/// JSON body of the GraphQL request querying the user's favorite materials in Prusa Connect.
std::string favorite_materials_request_body();

/// Parses the GraphQL response into a list of unique material uuids (in response order).
/// Returns nullopt if the response is malformed or contains GraphQL errors.
std::optional<std::vector<std::string>> parse_favorite_materials(const std::string& body);

} // namespace Slic3r::Biz::ConnectPrinter
