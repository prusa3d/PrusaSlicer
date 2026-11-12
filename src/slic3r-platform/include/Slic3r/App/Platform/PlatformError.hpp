#pragma once

#include "Slic3r/Exception.hpp"

namespace Slic3r::App::Platform {

class PlatformError : public CriticalException {
    using CriticalException::CriticalException;
};

}