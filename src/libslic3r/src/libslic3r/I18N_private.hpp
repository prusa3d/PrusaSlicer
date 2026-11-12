#pragma once

#include <string>
#include <functional>

namespace Slic3r {
namespace I18N_libslic3r {
    extern std::function<std::string(const char*)> translate_fn;
    inline std::string translate(const std::string &s) { return (translate_fn) ?  translate_fn(s.c_str()) : s; }
    inline std::string translate(const char *ptr) { return (translate_fn) ? translate_fn(ptr) : std::string(ptr); }
} // nameaspace I18N

[[maybe_unused]] inline const char* L(const char* s)    { return s; }
[[maybe_unused]] inline std::string _u8L(const char* s) { return I18N_libslic3r::translate(s); }

} // namespace Slic3r
