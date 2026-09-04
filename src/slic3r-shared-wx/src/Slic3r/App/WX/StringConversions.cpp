#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/Biz/Algorithms/StringUtils.hpp"
#include "Slic3r/Utils.hpp"

#include <wx/numformatter.h>

#include "LocalesUtils.hpp" //!


namespace Slic3r::App::WX {

wxString from_u8(const std::string& str)
{
    return wxString::FromUTF8(str.c_str());
}

wxString from_u8(const char* str)
{
    return wxString::FromUTF8(str);
}

std::string into_u8(const wxString& str)
{
    auto buffer_utf8 = str.utf8_str();
    return std::string(buffer_utf8.data());
}

wxString from_path(const boost::filesystem::path& path)
{
#ifdef _WIN32
    return wxString(path.string<std::wstring>());
#else
    return from_u8(path.string<std::string>());
#endif
}

boost::filesystem::path into_path(const wxString& str)
{
    return boost::filesystem::path(str.wx_str());
}

wxString double_to_string(double const value, const int max_precision /*= 4*/)
{
// Style_NoTrailingZeroes does not work on OSX. It also does not work correctly with some locales on Windows.
// return wxNumberFormatter::ToString(value, max_precision, wxNumberFormatter::Style_NoTrailingZeroes);

    wxString s = wxNumberFormatter::ToString(value, std::abs(value) < 0.0001 ? 10 : max_precision, wxNumberFormatter::Style_None);

    // The following code comes from wxNumberFormatter::RemoveTrailingZeroes(wxString& s)
    // with the exception that here one sets the decimal separator explicitely to dot.
    // If number is in scientific format, trailing zeroes belong to the exponent and cannot be removed.
    if (s.find_first_of(from_u8("eE")) == wxString::npos) {
        char dec_sep = is_decimal_separator_point() ? '.' : ',';
        const size_t posDecSep = s.find(dec_sep);
        // No decimal point => removing trailing zeroes irrelevant for integer number.
        if (posDecSep != wxString::npos) {
            // Find the last character to keep.
            size_t posLastNonZero = s.find_last_not_of(from_u8("0"));
            // If it's the decimal separator itself, don't keep it either.
            if (posLastNonZero == posDecSep)
                -- posLastNonZero;
            s.erase(posLastNonZero + 1);
            // Remove sign from orphaned zero.
            if (s.compare(from_u8("-0")) == 0)
                s = from_u8("0");
        }
    }

    return s;
}

wxString get_wraped_wxString(const wxString& in, size_t line_len /*=80*/)
{
    wxString out;

    for (size_t i = 0; i < in.size();) {
        // Overwrite the character (space or newline) starting at ibreak?
        bool   overwrite = false;
        // UTF8 representation of wxString.
        // Where to break the line, index of character at the start of a UTF-8 sequence.
        size_t ibreak    = size_t(-1);
        // Overwrite the character at ibreak (it is a whitespace) or not?
        size_t j = i;
        for (size_t cnt = 0; j < in.size();) {
            if (bool newline = in[j] == '\n'; in[j] == ' ' || in[j] == '\t' || newline) {
                // Overwrite the whitespace.
                ibreak    = j ++;
                overwrite = true;
                if (newline)
                    break;
            } else if (in[j] == '/'
#ifdef _WIN32
                 || in[j] == '\\'
#endif // _WIN32
                 ) {
                // Insert after the slash.
                ibreak    = ++ j;
                overwrite = false;
            } else
                j += get_utf8_sequence_length(static_cast<const char*>(in.ToUTF8()) + j, in.size() - j);
            if (++ cnt == line_len) {
                if (ibreak == size_t(-1)) {
                    ibreak    = j;
                    overwrite = false;
                }
                break;
            }
        }
        if (j == in.size()) {
            out.append(in.begin() + i, in.end());
            break;
        }
        assert(ibreak != size_t(-1));
        out.append(in.begin() + i, in.begin() + ibreak);
        out.append('\n');
        i = ibreak;
        if (overwrite)
            ++ i;
    }

    return out;
}

bool has_illegal_characters(const wxString& wx_string)
{
    const std::string str = into_u8(wx_string);
    return Biz::Algorithms::has_illegal_characters(str);
}

}

