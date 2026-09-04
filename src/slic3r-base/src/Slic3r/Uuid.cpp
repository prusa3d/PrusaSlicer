#include "Slic3r/Uuid.hpp"

#include <sstream>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace Slic3r {

std::string generate_uuid()
{
    boost::uuids::random_generator gen;
    std::ostringstream out;
    out << gen();
    return out.str();
}

} // namespace Slic3r
