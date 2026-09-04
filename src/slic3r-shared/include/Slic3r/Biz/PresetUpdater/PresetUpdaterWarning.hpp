#pragma once

#include "Slic3r/Biz/PresetUpdater/PresetUpdaterReason.hpp"

#include "fmt/format.h"
#include <string>

namespace Slic3r::Biz::PresetUpdater {

struct PresetUpdaterWarning
{
    std::string text;
    std::string repo;
    std::string vendor;
    PresetUpdaterReason reason{PresetUpdaterReason::Internal};

    std::string string() const
    {
        if (repo.empty() && vendor.empty()) {
            return text;
        }
        if (repo.empty()) {
             return fmt::format("{}: {}", vendor, text);
        }
        if (vendor.empty()) {
             return fmt::format("{}: {}", repo, text);
        }
        return fmt::format("{}/{}: {}",repo, vendor, text);
    }
};
}