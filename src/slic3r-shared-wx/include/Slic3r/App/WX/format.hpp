#pragma once

// Functional wrapper around boost::format.
// One day we may replace this wrapper with C++20 format
// https://en.cppreference.com/w/cpp/utility/format/format
// though C++20 format uses a different template pattern for position independent parameters.
// This wrapper also manages implicit conversion from wxString to UTF8 and format_wxstr() variants are provided to format into wxString.

#include <wx/string.h>

namespace Slic3r::internal::format {
	// Wrapper around wxScopedCharBuffer to indicate that the content is UTF8 formatted.
	struct utf8_buffer {
		// wxScopedCharBuffer is reference counted, therefore copying by value is cheap.
		wxScopedCharBuffer data;
	};
	inline std::ostream& operator<<(std::ostream& os, const utf8_buffer &v) {
		os << v.data.data();
		return os;
	}
	// Accept wxString and convert it to UTF8 to be processed by Slic3r::format().
	inline const utf8_buffer cook(const wxString& arg) {
		return utf8_buffer{ arg.ToUTF8() };
	}
	// Vojtech seemingly does not understand perfect forwarding:
	// Why Slic3r::internal::format::cook(T&& arg) is taken for non-const wxString reference?
	inline const utf8_buffer cook(wxString& arg) {
		return utf8_buffer{ arg.ToUTF8() };
	}
	inline const utf8_buffer cook(wxString&& arg) {
		return utf8_buffer{ arg.ToUTF8() };
	}
}

#include <Slic3r/LegacyFormat.hpp>

namespace Slic3r::App::WX {

// Format input mixing UTF8 encoded strings (const char*, std::string) and wxStrings, return a wxString.
template<typename... TArgs>
inline wxString format_wxstr(const char* fmt, TArgs&&... args) {
	boost::format message(fmt);
	return from_u8(Slic3r::internal::format::format_recursive(message, std::forward<TArgs>(args)...).c_str());
}
template<typename... TArgs>
inline wxString format_wxstr(const std::string& fmt, TArgs&&... args) {
	boost::format message(fmt);
	return from_u8(Slic3r::internal::format::format_recursive(message, std::forward<TArgs>(args)...).c_str());
}
template<typename... TArgs>
inline wxString format_wxstr(const wxString& fmt, TArgs&&... args) {
	return format_wxstr(fmt.ToUTF8().data(), std::forward<TArgs>(args)...);
}
template<typename... TArgs>
inline std::string format(const wxString& fmt, TArgs&&... args) {
    return Slic3r::format(fmt.ToUTF8().data(), std::forward<TArgs>(args)...);
}

} // namespace Slic3r::GUI
