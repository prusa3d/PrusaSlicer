#pragma once

#include "Slic3r/Biz/FileDownloader/FileDownloaderJob.hpp"
#include "Slic3r/Biz/FileDownloader/IFileDownloaderListener.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"

namespace Slic3r::Biz::FileDownloader {

class FileDownloaderInteractor : public WithListeners<IFileDownloaderListener>
{
public:
    FileDownloaderInteractor(Platform::IMainThreadDispatcher& dispatcher) : m_dispatcher(dispatcher)
    {}

    ~FileDownloaderInteractor()
    {
        ASSERT(
            m_dispatcher.is_closed(),
            "There must be no queued events (not even in the future),"
            " because they may remember the address of this instance!"
        );
    }

    void download_files_prusaslicer_url(const std::vector<std::string>& files_url);

    void init_multi_job(FileDownloaderMultiTicket ticket);

   

private:
    void init_download_job(FileDownloaderJobInput input_data);

    Platform::IMainThreadDispatcher& m_dispatcher;

    size_t m_next_id { 0 };
    size_t get_next_id() { return ++m_next_id; }
};
} // namespace Slic3r::Biz::FileDownloader
