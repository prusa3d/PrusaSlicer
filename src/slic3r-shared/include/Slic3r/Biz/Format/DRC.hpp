#pragma once

#include "Slic3r/Domain/TriangleMesh.hpp"
#include "tl/expected.hpp"

namespace Slic3r::Domain {
    class Model;
}

namespace Slic3r::Biz {

tl::expected<Domain::TriangleMesh, std::string> load_drc(const std::string& path);

}; // namespace Slic3r::Biz
