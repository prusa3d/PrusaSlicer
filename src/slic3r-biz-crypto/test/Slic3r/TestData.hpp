#pragma once
#include <boost/filesystem/operations.hpp>

namespace Slic3r {

inline boost::filesystem::path get_datadir() {
    namespace fs = boost::filesystem;
    return fs::absolute(fs::canonical(fs::path{TEST_DATA_DIR}));
}

} // namespace Slic3r
