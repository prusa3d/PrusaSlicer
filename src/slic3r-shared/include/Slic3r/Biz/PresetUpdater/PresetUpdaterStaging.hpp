#pragma once

#include <string>

#include <boost/filesystem/path.hpp>

namespace Slic3r::Biz::PresetUpdater {

/**
 * @brief Download space private to one install job, removed whole when the job ends.
 *
 * Every job gets its own directory under updater_staging_dir(), so no other job or application
 * instance ever reads, writes or reclaims it while it is in use. Nothing is created until the
 * first download asks for it.
 */
class StagingArea
{
public:
    StagingArea();
    StagingArea(StagingArea&&)                 = delete;
    StagingArea(const StagingArea&)            = delete;
    StagingArea& operator=(StagingArea&&)      = delete;
    StagingArea& operator=(const StagingArea&) = delete;
    ~StagingArea();

    /**
     * @brief Where one vendor version is downloaded to before it is installed.
     */
    boost::filesystem::path vendor_path(
        const std::string& repo_id,
        const std::string& vendor_id,
        const std::string& version
    ) const;

private:
    boost::filesystem::path m_root;
};

/**
 * @brief Drops what jobs that never finished left under updater_staging_dir().
 *
 * A job removes its own staging area, so anything left behind belongs to a job that crashed or
 * was killed. A job lasts minutes, so an area untouched for days cannot belong to a live one.
 * An area whose age cannot be read, for instance because it is being removed, is kept.
 */
void sweep_stale_staging();

} // namespace Slic3r::Biz::PresetUpdater
