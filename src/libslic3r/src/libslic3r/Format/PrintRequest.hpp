#ifndef slic3r_Format_PrintRequest_hpp_
#define slic3r_Format_PrintRequest_hpp_

namespace Slic3r::Domain {
class Model;
} // namespace Slic3r::Domain

namespace Slic3r {
bool load_printRequest(const char* input_file, Domain::Model* model);

} //namespace Slic3r 

#endif