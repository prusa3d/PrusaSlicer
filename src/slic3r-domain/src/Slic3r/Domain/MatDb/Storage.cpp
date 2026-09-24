#include "Slic3r/Domain/MatDb/Storage.hpp"

namespace Slic3r::Domain::MatDb {

void Index::clear()
{
    brands.clear();
    materials.clear();
}

} // namespace Slic3r::Domain::MatDb
