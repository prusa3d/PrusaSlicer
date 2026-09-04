#pragma once

namespace Slic3r::Biz::PresetUpdater {

enum class PresetUpdaterReason
{
    NoUsableVersion, ///< The source offers no version this application may use.
    IndexUnreadable, ///< A version index could not be read or parsed.
    DataUnreadable, ///< A file could not be read or downloaded from the source.
    DataCorrupted, ///< Hash mismatch, promised file absent, archive would not open.
    DataInconsistent, ///< A vendor's own version disagrees with the index recommending it.
    LocalStorageFailed, ///< A local file or directory could not be created, moved or removed.
    InstallFailed, ///< A vendor's install did not complete.
    SourceUnreachable, ///< Nothing at all could be read from a source.
    SourceListUnavailable, ///< The online list of sources could not be fetched or parsed.
    SourceDropped, ///< A local source's unzipped data is gone, so it left the list.
    ArchiveInvalid, ///< A zip the user picked is not a usable preset source.
    SourceNotFound, ///< An operation named a source that is not in the manifest.
    ManifestUnusable, ///< The stored source list could not be read or written.
    DataDirUnusable, ///< A directory the updater works in is missing or unusable.
    Internal, ///< Contract violations and unexpected exceptions.
};

} // namespace Slic3r::Biz::PresetUpdater
