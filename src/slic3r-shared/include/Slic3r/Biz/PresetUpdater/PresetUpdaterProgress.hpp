#pragma once

#include <cstddef>
#include <string>

namespace Slic3r::Biz::PresetUpdater {

enum class PresetUpdaterStep
{
    None, ///< No named subject. The job reports only that it is working.
    Downloading, ///< A vendor's data is being fetched from its source.
    Installing, ///< A vendor's data is being put in place.
    Removing, ///< A vendor's installed presets are being deleted.
};

/**
 * @brief What a preset updater job is doing right now, for the UI to word.
 *
 * The business layer never composes a sentence: it names the vendor and what is happening to it,
 * and leaves the wording, translation and ordering to whoever displays it. File names are
 * deliberately absent, being of no use to anyone but the log.
 */
struct PresetUpdaterProgress
{
    PresetUpdaterStep step{PresetUpdaterStep::None};
    std::string vendor_id;
    std::string version;
    /// One based position of the vendor in the batch. Zero until a batch is under way.
    std::size_t index{0};
    std::size_t total{0};
    int attempt{0};
    unsigned delay{0};
};

} // namespace Slic3r::Biz::PresetUpdater
