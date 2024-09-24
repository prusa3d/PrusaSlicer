#ifndef slic3r_Format_PrintRequest_hpp_
#define slic3r_Format_PrintRequest_hpp_



namespace Slic3r {
class Model;
bool load_printRequest(const char* input_file, Model* model);

} //namespace Slic3r 

#endif