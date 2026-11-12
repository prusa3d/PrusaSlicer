#ifndef slic3r_Format_SVG_hpp_
#define slic3r_Format_SVG_hpp_

#include <string>
#include "tl/expected.hpp"

namespace Slic3r::Domain { class Model; }
namespace Slic3r::Biz {

// Load an SVG file as embossed shape into a provided model.
tl::expected<Domain::Model, std::string> load_svg(const std::string &input_file);

}; // namespace Slic3r::Biz

#endif // slic3r_Format_SVG_hpp_ 
