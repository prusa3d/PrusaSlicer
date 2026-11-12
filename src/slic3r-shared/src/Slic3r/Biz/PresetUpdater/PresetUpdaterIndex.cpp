#include "PresetUpdaterIndex.hpp"

#include "Slic3r/Exception.hpp"

#include "Slic3r/Version.hpp"

#include <cctype>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/directory.hpp>
#include <boost/nowide/fstream.hpp>
#include <sstream>
#include <algorithm>

#ifdef _MSC_VER
#define strcasecmp _stricmp
#include <windows.h>
#endif

namespace Slic3r::Biz::PresetUpdater {

Semver SEMVER{SLIC3R_VERSION};

namespace {

// Generic file parser error, mostly copied from boost::property_tree::file_parser_error
class file_parser_error : public Slic3r::RuntimeError
{
public:
    file_parser_error(const std::string& msg, const std::string& file, unsigned long line = 0) :
        Slic3r::RuntimeError(format_what(msg, file, line)),
        m_message(msg),
        m_filename(file),
        m_line(line)
    {}

    file_parser_error(const std::string& msg, const boost::filesystem::path& file, unsigned long line = 0) :
        Slic3r::RuntimeError(format_what(msg, file.string(), line)),
        m_message(msg),
        m_filename(file.string()),
        m_line(line)
    {}

    // gcc 3.4.2 complains about lack of throw specifier on compiler
    // generated dtor
    ~file_parser_error() throw() {}

    // Get error message (without line and file - use what() to get full message)
    std::string message() const
    {
        return m_message;
    }

    // Get error filename
    std::string filename() const
    {
        return m_filename;
    }

    // Get error line number
    unsigned long line() const
    {
        return m_line;
    }

private:
    std::string m_message;
    std::string m_filename;
    unsigned long m_line;

    // Format error message to be returned by Slic3r::RuntimeError::what()
    static std::string format_what(const std::string& msg, const std::string& file, unsigned long l)
    {
        std::stringstream stream;
        stream << (file.empty() ? "<unspecified file>" : file.c_str());
        if (l > 0)
            stream << '(' << l << ')';
        stream << ": " << msg;
        return stream.str();
    }
};

// Unescape \n, \r and backslash
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
} // namespace

// Optimized lexicographic compare of two pre-release versions, ignoring the numeric suffix.
static int compare_prerelease(const char* p1, const char* p2)
{
    for (;;) {
        char c1 = *p1++;
        char c2 = *p2++;
        bool a1 = std::isalpha(c1) && c1 != 0;
        bool a2 = std::isalpha(c2) && c2 != 0;
        if (a1) {
            if (a2) {
                if (c1 != c2)
                    return (c1 < c2) ? -1 : 1;
            } else
                return 1;
        } else {
            if (a2)
                return -1;
            else
                return 0;
        }
    }
    // This shall never happen.
    return 0;
}

bool Version::is_slic3r_supported(const Slic3r::Semver& slic3r_version) const
{
    if (!slic3r_version.in_range(min_slic3r_version, max_slic3r_version))
        return false;
    // Now verify, whether the configuration pre-release status is compatible with the Slic3r's pre-release status.
    // Alpha Slic3r will happily load any configuration, while beta Slic3r will ignore alpha configurations etc.
    const char* prerelease_slic3r = slic3r_version.prerelease();
    const char* prerelease_config = this->config_version.prerelease();
    if (prerelease_config == nullptr)
        // Released config is always supported.
        return true;
    else if (prerelease_slic3r == nullptr)
        // Released slic3r only supports released configs.
        return false;
    // Compare the pre-release status of Slic3r against the config.
    // If the prerelease status of slic3r is lexicographically lower or equal
    // to the prerelease status of the config, accept it.
    return compare_prerelease(prerelease_slic3r, prerelease_config) != 1;
}

bool Version::is_current_slic3r_supported() const
{
    return this->is_slic3r_supported(SEMVER);
}

bool Version::is_current_slic3r_downgrade() const
{
    return SEMVER < min_slic3r_version;
}

inline char* left_trim(char* c)
{
    for (; *c == ' ' || *c == '\t'; ++c)
        ;
    return c;
}

inline char* right_trim(char* start)
{
    char* end = start + strlen(start) - 1;
    for (; end >= start && (*end == ' ' || *end == '\t'); --end)
        ;
    *(++end) = 0;
    return end;
}

inline std::string unquote_value(char* value, char* end, const std::string& path, int idx_line)
{
    std::string svalue;
    if (value == end) {
        // Empty string is a valid string.
    } else if (*value == '"') {
        if (++value > --end || *end != '"')
            throw file_parser_error("String not enquoted correctly", path, idx_line);
        *end = 0;
        if (!unescape_string_cstyle(value, svalue))
            throw file_parser_error("Invalid escape sequence inside a quoted string", path, idx_line);
    } else
        svalue.assign(value, end);
    return svalue;
}

inline std::string unquote_version_comment(char* value, char* end, const std::string& path, int idx_line)
{
    std::string svalue;
    if (value == end) {
        // Empty string is a valid string.
    } else if (*value == '"') {
        if (++value > --end || *end != '"')
            throw file_parser_error("Version comment not enquoted correctly", path, idx_line);
        *end = 0;
        if (!unescape_string_cstyle(value, svalue))
            throw file_parser_error(
                "Invalid escape sequence inside a quoted version comment",
                path,
                idx_line
            );
    } else
        svalue.assign(value, end);
    return svalue;
}

size_t PresetUpdaterIndex::load(const boost::filesystem::path& path)
{
    m_configs.clear();
    m_vendor = path.stem().string();
    m_path   = path;

    boost::nowide::ifstream ifs(path.string());
    std::string line;
    size_t idx_line = 0;
    Version ver;
    while (std::getline(ifs, line)) {
#ifndef _MSVCVER
        // On a Unix system, getline does not remove the trailing carriage returns, if the index is shared over a Windows filesystem. Remove them manually.
        while (!line.empty() && line.back() == '\r')
            line.pop_back();
#endif
        ++idx_line;
        // Skip the initial white spaces.
        char* key = left_trim(line.data());
        if (*key == '#')
            // Skip a comment line.
            continue;
        // Right trim the line.
        char* end = right_trim(key);
        if (key == end)
            // Skip an empty line.
            continue;
        // Keyword may only contain alphanumeric characters. Semantic version may in addition contain "+.-".
        char* key_end     = key;
        bool maybe_semver = true;
        for (; *key_end != 0; ++key_end) {
            if (std::isalnum(*key_end) || strchr("+.-", *key_end) != nullptr) {
                // It may be a semver.
            } else if (*key_end == '_') {
                // Cannot be a semver, but it may be a key.
                maybe_semver = false;
            } else
                // End of semver or keyword.
                break;
        }
        if (*key_end != 0 && *key_end != ' ' && *key_end != '\t' && *key_end != '=')
            throw file_parser_error("Invalid keyword or semantic version", path, idx_line);
        char* value         = left_trim(key_end);
        bool key_value_pair = *value == '=';
        if (key_value_pair)
            value = left_trim(value + 1);
        *key_end = 0;
        boost::optional<Slic3r::Semver> semver;
        if (maybe_semver)
            semver = Slic3r::Semver::parse(key);
        if (key_value_pair) {
            if (semver)
                throw file_parser_error("Key cannot be a semantic version", path, idx_line);
            // Verify validity of the key / value pair.
            std::string svalue = unquote_value(value, end, path.string(), idx_line);
            if (strcmp(key, "min_slic3r_version") == 0 || strcmp(key, "max_slic3r_version") == 0) {
                if (!svalue.empty())
                    semver = Slic3r::Semver::parse(svalue);
                if (!semver)
                    throw file_parser_error(
                        std::string(key) + " must referece a valid semantic version",
                        path,
                        idx_line
                    );
                if (strcmp(key, "min_slic3r_version") == 0)
                    ver.min_slic3r_version = *semver;
                else
                    ver.max_slic3r_version = *semver;
            } else {
                // Ignore unknown keys, as there may come new keys in the future.
            }
            continue;
        }
        if (!semver)
            throw file_parser_error("Invalid semantic version", path, idx_line);
        ver.config_version = *semver;
        ver.comment        = (end <= key_end) ?
                   "" :
                   unquote_version_comment(value, end, path.string(), idx_line);
        m_configs.emplace_back(ver);
    }

    // Sort the configs by their version.
    std::sort(m_configs.begin(), m_configs.end(), [](const Version& v1, const Version& v2) {
        return v1.config_version < v2.config_version;
    });
    return m_configs.size();
}

Slic3r::Semver PresetUpdaterIndex::version() const
{
    Slic3r::Semver ver = Slic3r::Semver::zero();
    for (const Version& cv : m_configs)
        if (cv.config_version >= ver)
            ver = cv.config_version;
    return ver;
}

PresetUpdaterIndex::const_iterator PresetUpdaterIndex::find(const Slic3r::Semver& ver) const
{
    Version key;
    key.config_version = ver;
    auto it            = std::lower_bound(
        m_configs.begin(),
        m_configs.end(),
        key,
        [](const Version& v1, const Version& v2) { return v1.config_version < v2.config_version; }
    );
    return (it == m_configs.end() || it->config_version == ver) ? it : m_configs.end();
}

PresetUpdaterIndex::const_iterator PresetUpdaterIndex::recommended(const Slic3r::Semver& slic3r_version) const
{
    const_iterator highest = this->end();
    for (const_iterator it = this->begin(); it != this->end(); ++it)
        if (it->is_slic3r_supported(slic3r_version)
            && (highest == this->end() || highest->config_version < it->config_version))
            highest = it;
    return highest;
}

PresetUpdaterIndex::const_iterator PresetUpdaterIndex::recommended() const
{
    return this->recommended(SEMVER);
}

namespace {
// Ignore system and hidden files, which may be created by the DropBox synchronisation process.
// https://github.com/prusa3d/PrusaSlicer/issues/1298
bool is_plain_file(const boost::filesystem::directory_entry& dir_entry)
{
    if (!boost::filesystem::is_regular_file(dir_entry.status()))
        return false;
#ifdef _MSC_VER
    DWORD attributes = GetFileAttributesW(boost::nowide::widen(dir_entry.path().string()).c_str());
    return (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) == 0;
#else
    return true;
#endif
}
} // namespace

bool is_idx_file(const boost::filesystem::directory_entry& dir_entry)
{
    return is_plain_file(dir_entry)
        && strcasecmp(dir_entry.path().extension().string().c_str(), ".idx") == 0;
}

} // namespace Slic3r::Biz::PresetUpdater
