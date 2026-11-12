#include "Slic3r/Biz/PrintHost/PrintHostSL1Host.hpp"

#include <boost/algorithm/string.hpp>

namespace Slic3r::Biz::PrintHost {
bool PrintHostSL1Host::validate_version_text(const boost::optional<std::string>& version_text) const
{
     return version_text ? boost::starts_with(*version_text, "Prusa SLA") : false;
}
} // namespace Slic3r::Biz::PrintHost