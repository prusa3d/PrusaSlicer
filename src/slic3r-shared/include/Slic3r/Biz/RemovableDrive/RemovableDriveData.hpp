#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <boost/filesystem/path.hpp>
#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace Slic3r::Biz::RemovableDrive {

struct DriveData
{
    std::string name;
    boost::filesystem::path path;

    void clear()
    {
        name.clear();
        path.clear();
    }

    bool empty() const
    {
        return path.empty();
    }
};

inline bool operator<(const DriveData& lhs, const DriveData& rhs)
{
    return lhs.path < rhs.path;
}

inline bool operator>(const DriveData& lhs, const DriveData& rhs)
{
    return lhs.path > rhs.path;
}

inline bool operator==(const DriveData& lhs, const DriveData& rhs)
{
    return lhs.path == rhs.path;
}

inline std::vector<DriveData>::const_iterator
find_drive(const std::vector<DriveData>& sorted_drives, const boost::filesystem::path& p)
{
    auto is_ancestor = [](const boost::filesystem::path& base, const boost::filesystem::path& path_to_check)
    {
        auto base_it = base.begin();
        auto path_it = path_to_check.begin();
        while (base_it != base.end() && path_it != path_to_check.end() && *base_it == *path_it) {
            ++base_it;
            ++path_it;
        }
        return base_it == base.end();
    };

    // Find a candidate by binary search (first before result).
    // Than confirm by is_ancestor() call.
    DriveData search_val;
    search_val.path = p.lexically_normal();
    auto it         = std::upper_bound(sorted_drives.begin(), sorted_drives.end(), search_val);
    if (it == sorted_drives.begin()) {
        return sorted_drives.end();
    }
    auto candidate = std::prev(it);
    if (is_ancestor(candidate->path, search_val.path)) {
        return candidate;
    }
    return sorted_drives.end();
}

#ifndef _WIN32
/**
 * @brief Matches by device id of p or of its nearest existing ancestor.
 */
inline std::vector<DriveData>::const_iterator
find_drive_by_device(const std::vector<DriveData>& drives, boost::filesystem::path p)
{
    struct stat buf;
    while (!p.empty() && ::stat(p.c_str(), &buf) != 0) {
        p = p.parent_path();
    }
    if (p.empty()) {
        return drives.end();
    }
    return std::find_if(drives.begin(), drives.end(), [dev = buf.st_dev](const DriveData& drive) {
        struct stat drive_buf;
        return ::stat(drive.path.c_str(), &drive_buf) == 0 && drive_buf.st_dev == dev;
    });
}
#endif

/**
 * @brief Returns vector of added and vector of removed drives.
 */
inline std::pair<std::vector<DriveData>, std::vector<DriveData>>
get_drive_changes(const std::vector<DriveData>& sorted_old_drives, const std::vector<DriveData>& sorted_new_drives)
{
    std::vector<DriveData> added;
    std::vector<DriveData> deleted;

    // Items in new_drives but not in old_drives are "added"
    std::set_difference(
        sorted_new_drives.begin(),
        sorted_new_drives.end(),
        sorted_old_drives.begin(),
        sorted_old_drives.end(),
        std::back_inserter(added)
    );

    // Items in old_drives but not in new_drives are "deleted"
    std::set_difference(
        sorted_old_drives.begin(),
        sorted_old_drives.end(),
        sorted_new_drives.begin(),
        sorted_new_drives.end(),
        std::back_inserter(deleted)
    );

    return {added, deleted};
}
} // namespace Slic3r::Biz::RemovableDrive
