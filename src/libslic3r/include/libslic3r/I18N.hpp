#pragma once

#include <string>
#include <functional>

namespace Slic3r::I18N_libslic3r {
    void set_translate_callback(std::function<std::string(const char*)> fn);
}
