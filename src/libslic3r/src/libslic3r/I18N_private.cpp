#include "libslic3r/I18N.hpp"

#include "I18N_private.hpp"

namespace Slic3r::I18N_libslic3r {

std::function<std::string(const char*)> translate_fn;

void set_translate_callback(std::function<std::string(const char*)> fn)
{
    translate_fn = fn;
}

} // namespace Slic3r::I18N_libslic3r
