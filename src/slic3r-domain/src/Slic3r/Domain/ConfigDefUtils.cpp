#include "Slic3r/Domain/ConfigDefUtils.hpp"
#include "Slic3r/Domain/ConfigValue.hpp"

namespace Slic3r::Domain {


std::vector<EnumValueDefsPtr>& get_enum_defs() {
    static std::vector<EnumValueDefsPtr> result;
    return result;
}

}
