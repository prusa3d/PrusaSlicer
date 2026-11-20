#pragma once

#include "Slic3r/Exception.hpp"

namespace Slic3r::Biz::Preset {

struct PresetEvalException : Exception
{
    using Exception::Exception;
};

} // namespace Slic3r::Biz::Preset
