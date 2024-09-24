#ifndef slic3r_DirectoriesUtils_hpp_
#define slic3r_DirectoriesUtils_hpp_

#include <string>

#if __APPLE__
//implemented at MacUtils.mm
std::string GetDataDir();
#endif //__APPLE__

namespace Slic3r {

std::string get_default_datadir();

} // namespace Slic3r

#endif // slic3r_DirectoriesUtils_hpp_
