#pragma once

#include "Slic3r/Biz/I18N/I18N.hpp"

#include <wx/string.h>

namespace Slic3r::App::WX {

extern wxString _(const std::string& s);
extern wxString _(const wxString& s);
extern wxString _L(const std::string& s);
extern wxString _CTX(const std::string& s, const std::string& ctx);
extern wxString _L_PLURAL(const std::string& single, const std::string& plural, int n);

}

