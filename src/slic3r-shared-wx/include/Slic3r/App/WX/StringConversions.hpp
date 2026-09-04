#pragma once

#include <wx/string.h>

#include <boost/filesystem.hpp>

namespace Slic3r::App::WX {

// wxString from std::string in UTF8
wxString	from_u8(const std::string& str);
wxString	from_u8(const char* str);
// std::string in UTF8 from wxString
std::string	into_u8(const wxString& str);
// wxString from boost path
wxString	from_path(const boost::filesystem::path& path);
// boost path from wxString
boost::filesystem::path	into_path(const wxString& str);

wxString double_to_string(double const value, const int max_precision = 4);

wxString get_wraped_wxString(const wxString& text_in, size_t line_len = 80);

static wxString dots("…", wxConvUTF8);

bool has_illegal_characters(const wxString& wxstring);

}

