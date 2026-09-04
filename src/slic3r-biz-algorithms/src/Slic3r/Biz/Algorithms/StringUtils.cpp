#include "Slic3r/Biz/Algorithms/StringUtils.hpp"
#include "Slic3r/Assert.hpp"
#include <algorithm>
#include <vector>

namespace Slic3r::Biz::Algorithms {

/**
 * List of illegal characters
 */
static constexpr char illegal_characters[] = "<>:/\\|?*\"";

/**
 * Function to detect containing of the illegal characters
 */
bool has_illegal_characters(const std::string& str)
{
    for (size_t i = 0; i < std::strlen(illegal_characters); i++)
        if (str.find_first_of(illegal_characters[i]) != std::string::npos)
            return true;

    return false;
}

std::string escape_string_cstyle(const std::string& str)
{
    // Allocate a buffer twice the input string length,
    // so the output will fit even if all input characters get escaped.
    std::vector<char> out(str.size() * 2, 0);
    char* outptr = out.data();
    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];
        if (c == '\r') {
            (*outptr++) = '\\';
            (*outptr++) = 'r';
        } else if (c == '\n') {
            (*outptr++) = '\\';
            (*outptr++) = 'n';
        } else if (c == '\\') {
            (*outptr++) = '\\';
            (*outptr++) = '\\';
        } else
            (*outptr++) = c;
    }
    return std::string(out.data(), outptr - out.data());
}

std::string escape_strings_cstyle(const std::vector<std::string>& strs)
{
    // 1) Estimate the output buffer size to avoid buffer reallocation.
    size_t outbuflen = 0;
    for (size_t i = 0; i < strs.size(); ++i)
        // Reserve space for every character escaped + quotes + semicolon.
        outbuflen += strs[i].size() * 2 + 3;
    // 2) Fill in the buffer.
    std::vector<char> out(outbuflen, 0);
    char* outptr = out.data();
    for (size_t j = 0; j < strs.size(); ++j) {
        if (j > 0)
            // Separate the strings.
            (*outptr++) = ';';
        const std::string& str = strs[j];
        // Is the string simple or complex? Complex string contains spaces, tabs, new lines and other
        // escapable characters. Empty string shall be quoted as well, if it is the only string in strs.
        bool should_quote = strs.size() == 1 && str.empty();
        for (size_t i = 0; i < str.size(); ++i) {
            char c = str[i];
            if (c == ' ' || c == ';' || c == '\t' || c == '\\' || c == '"' || c == '\r' || c == '\n') {
                should_quote = true;
                break;
            }
        }
        if (should_quote) {
            (*outptr++) = '"';
            for (size_t i = 0; i < str.size(); ++i) {
                char c = str[i];
                if (c == '\\' || c == '"') {
                    (*outptr++) = '\\';
                    (*outptr++) = c;
                } else if (c == '\r') {
                    (*outptr++) = '\\';
                    (*outptr++) = 'r';
                } else if (c == '\n') {
                    (*outptr++) = '\\';
                    (*outptr++) = 'n';
                } else
                    (*outptr++) = c;
            }
            (*outptr++) = '"';
        } else {
            memcpy(outptr, str.data(), str.size());
            outptr += str.size();
        }
    }
    return std::string(out.data(), outptr - out.data());
}

bool unescape_string_cstyle(const std::string& str, std::string& str_out)
{
    std::vector<char> out(str.size(), 0);
    char* outptr = out.data();
    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];
        if (c == '\\') {
            if (++i == str.size())
                return false;
            c = str[i];
            if (c == 'r')
                (*outptr++) = '\r';
            else if (c == 'n')
                (*outptr++) = '\n';
            else
                (*outptr++) = c;
        } else
            (*outptr++) = c;
    }
    str_out.assign(out.data(), outptr - out.data());
    return true;
}

bool unescape_strings_cstyle(const std::string& str, std::vector<std::string>& out)
{
    if (str.empty())
        return true;

    size_t i = 0;
    for (;;) {
        // Skip white spaces.
        char c = str[i];
        while (c == ' ' || c == '\t') {
            if (++i == str.size())
                return true;
            c = str[i];
        }
        // Start of a word.
        std::vector<char> buf;
        buf.reserve(16);
        // Is it enclosed in quotes?
        c = str[i];
        if (c == '"') {
            // Complex case, string is enclosed in quotes.
            for (++i; i < str.size(); ++i) {
                c = str[i];
                if (c == '"') {
                    // End of string.
                    break;
                }
                if (c == '\\') {
                    if (++i == str.size())
                        return false;
                    c = str[i];
                    if (c == 'r')
                        c = '\r';
                    else if (c == 'n')
                        c = '\n';
                }
                buf.push_back(c);
            }
            if (i == str.size())
                return false;
            ++i;
        } else {
            for (; i < str.size(); ++i) {
                c = str[i];
                if (c == ';')
                    break;
                buf.push_back(c);
            }
        }
        // Store the string into the output vector.
        out.push_back(std::string(buf.data(), buf.size()));
        if (i == str.size())
            return true;
        // Skip white spaces.
        c = str[i];
        while (c == ' ' || c == '\t') {
            if (++i == str.size())
                // End of string. This is correct.
                return true;
            c = str[i];
        }
        if (c != ';')
            return false;
        if (++i == str.size()) {
            // Emit one additional empty string.
            out.push_back(std::string());
            return true;
        }
    }
}

std::string to_lower_ascii(std::string_view data)
{
    std::string out;
    out.reserve(data.size());
    std::transform(
        data.begin(),
        data.end(),
        std::back_inserter(out),
        [](std::string_view::value_type v)
        {
            // Only ASCII is allowed here, handling UTF8 string would require a way more complicated
            // logic as 1 character can have variable length (1..3 bytes)
            ASSERT(static_cast<unsigned char>(v) <= 127);
            return std::tolower(v);
        }
    );

    return out;
}

} // namespace Slic3r::Biz::Algorithms
