#pragma once

#include <charconv>
#include <optional>
#include <sstream>
#include <string>

namespace Slic3r::Biz::RemovableDrive {

struct MountInfoEntry
{
    std::string mount_point; // Where the filesystem is mounted, unescaped.
    std::string source;      // What it is mounted from, a device node for a real disk.
};

/**
 * @brief Undoes the escaping /proc/self/mountinfo applies to its path fields.
 * Spaces, tabs, newlines and backslashes are written as a backslash followed by
 * three octal digits, so that no field can contain whitespace.
 */
inline std::string unescape_mount_info_field(const std::string& field)
{
    std::string unescaped;
    unescaped.reserve(field.size());

    for (std::size_t i = 0; i < field.size(); ++i) {
        if (field[i] == '\\' && i + 3 < field.size()) {
            const char* first = field.data() + i + 1;
            const char* last  = first + 3;

            unsigned value{};
            if (std::from_chars(first, last, value, 8).ptr == last) {
                unescaped += static_cast<char>(value);
                i += 3;
                continue;
            }
        }
        unescaped += field[i];
    }

    return unescaped;
}

/**
 * @brief Reads the mount point and the mount source out of one mountinfo line.
 * Returns nothing for a line that does not have them, which includes the empty
 * line and anything that is not mountinfo at all.
 *
 * 36 35 98:0 /mnt1 /mnt2 rw,noatime shared:1 - ext3 /dev/root rw,errors=continue
 *                        ^ field 5                  ^ separator
 *                                                     ^ type  ^ source
 *
 * The number of fields between the mount point and the separator varies, which
 * is why the separator exists.
 */
inline std::optional<MountInfoEntry> parse_mount_info_line(const std::string& line)
{
    std::istringstream fields(line);
    std::string        field;

    std::string mount_point;
    for (int i = 0; i < 5; ++i) {
        if (!(fields >> field)) {
            return std::nullopt;
        }
        mount_point = field;
    }

    // Skip the optional fields, however many there are, and the separator.
    while (fields >> field && field != "-") {}
    if (field != "-") {
        return std::nullopt;
    }

    std::string type;
    std::string source;
    if (!(fields >> type >> source)) {
        return std::nullopt;
    }

    return MountInfoEntry{unescape_mount_info_field(mount_point), unescape_mount_info_field(source)};
}

} // namespace Slic3r::Biz::RemovableDrive
