#pragma once

#include <string>
#include "tl/expected.hpp"

namespace Slic3r::Domain {
class TriangleMesh;
} // namespace Slic3r::Domain

namespace Slic3r::Biz {

tl::expected<Domain::TriangleMesh, std::string> load_stl(const std::string& path);
bool store_stl(
    const std::string& path,
    const Domain::TriangleMesh& mesh,
    bool binary,
    const std::string& label = ""
);

} // namespace Slic3r::Biz
