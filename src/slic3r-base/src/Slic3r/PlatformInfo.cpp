#include "Slic3r/PlatformInfo.hpp"

namespace Slic3r {



PlatformInfo& PlatformInfo::instance()
{
    static PlatformInfo inst;
    return inst;
}


}
