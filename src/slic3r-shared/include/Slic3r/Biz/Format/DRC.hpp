#pragma once

#include "Slic3r/Domain/TriangleMesh.hpp"
#include "tl/expected.hpp"

namespace Slic3r::Domain {
    class Model;
}

namespace Slic3r::Biz {

constexpr int DRC_BITS_MIN = 8;
constexpr int DRC_BITS_MAX = 30;
constexpr int DRC_BITS_DEFAULT = 0;
constexpr int DRC_SPEED_DEFAULT = 0;
constexpr char DRC_BITS_DEFAULT_STR[] = "0";

tl::expected<Domain::TriangleMesh, std::string> load_drc(const std::string& path);
bool store_drc(const std::string& path, const Domain::TriangleMesh& mesh, int bits = DRC_BITS_DEFAULT, int speed = DRC_SPEED_DEFAULT);
bool store_drc(const std::string& path, Domain::Model* model, int bits = DRC_BITS_DEFAULT, int speed = DRC_SPEED_DEFAULT);

}; // namespace Slic3r::Biz
