#pragma once

#include "Slic3r/Domain/TriangleMesh.hpp"
#include "tl/expected.hpp"

namespace Slic3r::Domain {
    class Model;
}

namespace Slic3r::Biz {

tl::expected<Domain::TriangleMesh, std::string> load_obj(const std::string& path);
bool store_obj(const std::string& path, const Domain::TriangleMesh& mesh);
bool store_obj(const std::string& path, Domain::Model* model);

}; // namespace Slic3r::Biz
