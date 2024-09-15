///|/ Copyright (c) Prusa Research 2022 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Oleksandra Iushchenko @YuSanka
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "FindReplace.hpp"

#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/find.hpp>
#include <boost/range/iterator_range_core.hpp>
#include <cctype> // isalpha
#include <exception>
#include <iterator>
#include <tuple>
#include <utility>
#include <cstring>

#include "../Utils.hpp"
#include "libslic3r/Exception.hpp"

namespace Slic3r {

// Similar to https://npp-user-manual.org/docs/searching/#extended-search-mode
const void unescape_extended_search_mode(std::string &s)
{
    boost::replace_all(s, "\\n", "\n"); // Line Feed control character LF (ASCII 0x0A)
    boost::replace_all(s, "\\r", "\r"); // Carriage Return control character CR (ASCII 0x0D)
    boost::replace_all(s, "\\t", "\t"); // TAB control character (ASCII 0x09)
    boost::replace_all(s, "\\0", "\0x00"); // NUL control character (ASCII 0x00)
    boost::replace_all(s, "\\\\", "\\"); // Backslash character (ASCII 0x5C)

// Notepad++ also supports:
// \o: the octal representation of a byte, made of 3 digits in the 0-7 range
// \d: the decimal representation of a byte, made of 3 digits in the 0-9 range
// \x: the hexadecimal representation of a byte, made of 2 digits in the 0-9, A-F/a-f range.
// \u: The hexadecimal representation of a two-byte character, made of 4 digits in the 0-9, A-F/a-f range.
}

GCodeFindReplace::GCodeFindReplace(const std::vector<std::string> &gcode_substitutions)
{
    if ((gcode_substitutions.size() % 4) != 0)
        throw RuntimeError("Invalid length of gcode_substitutions parameter");

    m_substitutions.reserve(gcode_substitutions.size() / 4);
    for (size_t i = 0; i < gcode_substitutions.size(); i += 4) {
        Substitution out;
        try {
            out.plain_pattern    = gcode_substitutions[i];
            out.format           = gcode_substitutions[i + 1];
            const std::string &params = gcode_substitutions[i + 2];
            out.regexp           = strchr(params.c_str(), 'r') != nullptr || strchr(params.c_str(), 'R') != nullptr;
            out.case_insensitive = strchr(params.c_str(), 'i') != nullptr || strchr(params.c_str(), 'I') != nullptr;
            out.whole_word       = strchr(params.c_str(), 'w') != nullptr || strchr(params.c_str(), 'W') != nullptr;
            out.single_line      = strchr(params.c_str(), 's') != nullptr || strchr(params.c_str(), 'S') != nullptr;
            if (out.regexp) {
                out.regexp_pattern.assign(
                    out.whole_word ? 
                        std::string("\\b") + out.plain_pattern + "\\b" :
                        out.plain_pattern,
                    (out.case_insensitive ? boost::regex::icase : 0) | boost::regex::optimize);
            } else {
                unescape_extended_search_mode(out.plain_pattern);
                unescape_extended_search_mode(out.format);
            }
        } catch (const std::exception &ex) {
            throw RuntimeError(std::string("Invalid gcode_substitutions parameter, failed to compile regular expression: ") + ex.what());
        }
        m_substitutions.emplace_back(std::move(out));
    }
}

class ToStringIterator 
{
public:
    using iterator_category     = std::output_iterator_tag;
    using value_type            = void;
    using difference_type       = void;
    using pointer               = void;
    using reference             = void;

    ToStringIterator(std::string &data) : m_data(&data) {}

    ToStringIterator& operator=(const char val) {
        size_t needs = m_data->size() + 1;
        if (m_data->capacity() < needs)
            m_data->reserve(next_highest_power_of_2(needs));
        m_data->push_back(val);
        return *this;
    }

    ToStringIterator& operator*()     { return *this; }
    ToStringIterator& operator++()    { return *this; }
    ToStringIterator  operator++(int) { return *this; }

private:
    std::string *m_data;
};

template<typename FindFn>
static void find_and_replace_whole_word(std::string &inout, const std::string &match, const std::string &replace, FindFn find_fn)
{
    if (! match.empty() && inout.size() >= match.size() && match != replace) {
        std::string out;
        auto [i, j] = find_fn(inout, 0, match);
        size_t k = 0;
        for (; i != std::string::npos; std::tie(i, j) = find_fn(inout, i, match)) {
            if ((i == 0 || ! std::isalnum(inout[i - 1])) && (j == inout.size() || ! std::isalnum(inout[j]))) {
                out.reserve(inout.size());
                out.append(inout, k, i - k);
                out.append(replace);
                i = k = j;
            } else
                i += match.size();
        }
        if (k > 0) {
            out.append(inout, k, inout.size() - k);
            inout.swap(out);
        }
    }
}

std::string GCodeFindReplace::process_layer(const std::string &ain)
{
    std::string out;
    const std::string *in = &ain;
    std::string temp;
    temp.reserve(in->size());

    for (const Substitution &substitution : m_substitutions) {
        if (substitution.regexp) {
            temp.clear();
            temp.reserve(in->size());
            boost::regex_replace(ToStringIterator(temp), in->begin(), in->end(),
                substitution.regexp_pattern, substitution.format, 
                (substitution.single_line ? boost::match_single_line | boost::match_default : boost::match_not_dot_newline | boost::match_default) | boost::format_all);
            std::swap(out, temp);
        } else {
            if (in == &ain)
                out = ain;
            // Plain substitution
            if (substitution.case_insensitive) {
                if (substitution.whole_word)
                    find_and_replace_whole_word(out, substitution.plain_pattern, substitution.format,
                        [](const std::string &str, size_t start_pos, const std::string &match) {
                            auto begin = str.begin() + start_pos;
                            boost::iterator_range<std::string::const_iterator> r1(begin, str.end());
                            boost::iterator_range<std::string::const_iterator> r2(match.begin(), match.end());
                            auto res = boost::ifind_first(r1, r2);
                            return res ? std::make_pair(size_t(res.begin() - str.begin()), size_t(res.end() - str.begin())) : std::make_pair(std::string::npos, std::string::npos);
                        });
                else
                    boost::ireplace_all(out, substitution.plain_pattern, substitution.format);
            } else {
                if (substitution.whole_word)
                    find_and_replace_whole_word(out, substitution.plain_pattern, substitution.format,
                        [](const std::string &str, size_t start_pos, const std::string &match) { 
                            size_t pos = str.find(match, start_pos);
                            return std::make_pair(pos, pos + (pos == std::string::npos ? 0 : match.size()));
                        });
                else
                    boost::replace_all(out, substitution.plain_pattern, substitution.format);
            }
        }
        in = &out;
    }

    return out;
}

}
