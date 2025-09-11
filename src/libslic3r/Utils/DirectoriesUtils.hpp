#ifndef slic3r_DirectoriesUtils_hpp_
#define slic3r_DirectoriesUtils_hpp_

#include <string>
#include <boost/filesystem.hpp>
#include <optional>

#if __APPLE__
//implemented at MacUtils.mm
std::string GetDataDir();
#endif //__APPLE__

namespace Slic3r {

// Only defined on linux.
std::optional<boost::filesystem::path> get_home_config_dir();
std::optional<boost::filesystem::path> get_home_local_dir();

std::string get_default_datadir();

} // namespace Slic3r

#endif // slic3r_DirectoriesUtils_hpp_
