#pragma once

#include <string>

namespace Slic3r::Biz::Slicing {
struct SerializedConfig {
    std::string json;
    std::string ini;
};
}

