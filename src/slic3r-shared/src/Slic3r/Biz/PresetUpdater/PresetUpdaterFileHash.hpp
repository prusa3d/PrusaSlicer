#pragma once

#include "Slic3r/Biz/PresetUpdater/PresetUpdaterProcessStatus.hpp"

#include <string>

namespace boost::filesystem {
class path;
} // namespace boost::filesystem

namespace Slic3r::Biz::PresetUpdater {

class PresetUpdaterFileHash
{
public:
    PresetUpdaterFileHash(const std::string hash);
    bool compare(const std::string& hash_string) const;

    friend bool operator==(const PresetUpdaterFileHash& lhs, const PresetUpdaterFileHash& rhs);

private:
    std::string m_lower_case;
    std::string m_upper_case;
};

inline bool operator==(const PresetUpdaterFileHash& lhs, const PresetUpdaterFileHash& rhs)
{
    return lhs.m_lower_case == rhs.m_lower_case;
}

inline bool operator!=(const PresetUpdaterFileHash& lhs, const PresetUpdaterFileHash& rhs)
{
    return !(lhs == rhs);
}

inline bool operator==(const PresetUpdaterFileHash& lhs, const std::string& rhs)
{
    return lhs.compare(rhs);
}

inline bool operator==(const std::string& lhs, const PresetUpdaterFileHash& rhs)
{
    return rhs.compare(lhs);
}

inline bool operator!=(const PresetUpdaterFileHash& lhs, const std::string& rhs)
{
    return !lhs.compare(rhs);
}

inline bool operator!=(const std::string& lhs, const PresetUpdaterFileHash& rhs)
{
    return !rhs.compare(lhs);
}

PresetUpdaterFileHash file_hash(
    const boost::filesystem::path& path,
    PresetUpdaterProcessStatus* process_status
);

} // namespace Slic3r::Biz::PresetUpdater
