#include "Slic3r/TypeInfo.hpp"
#ifdef __GNUC__ // if GCC or Clang
#include <cxxabi.h>
#include <cstdlib>
#endif

namespace Slic3r::Internal {

std::string demangle_name(const char* name)
{
#ifdef __GNUC__
    int status;
    char* demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    std::string ret = status == 0 ? demangled : name;
    if (demangled)
        std::free(demangled);
    return ret;
#else
    return name;
#endif
}


}
