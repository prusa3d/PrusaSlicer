#include "Slic3r/Biz/Crypto/ContentProvider.hpp"

#include "Slic3r/Biz/Algorithms/ZipArchive.hpp"

#include <boost/filesystem/directory.hpp>
#include <boost/filesystem/operations.hpp>
#include <utility>

namespace Slic3r::Biz::Crypto {
class DirContentSource : public IContentProvider
{
public:
    explicit DirContentSource(const std::string& dir_path) :
        m_dir_path(dir_path),
        m_dir_path_str(dir_path)
    {}

    FileList list_files() override
    {
        FileList file_list;
        for (const auto& entry : boost::filesystem::recursive_directory_iterator(m_dir_path)) {
            if (entry.is_directory()) {
                continue;
            }

            file_list.push_back(boost::filesystem::relative(entry.path(), m_dir_path).string());
        }

        return file_list;
    }

    bool file_stream(const std::string& file_path, const StreamKernel& kernel, size_t buffer_size) override
    {
        return Crypto::file_stream((m_dir_path / file_path).string(), kernel, buffer_size);
    }

    const std::string& path() const override
    {
        return m_dir_path_str;
    }

private:
    const boost::filesystem::path m_dir_path;
    const std::string m_dir_path_str;
};

IContentProviderPtr create_directory_source(const std::string& dir_path)
{
    return std::make_unique<DirContentSource>(dir_path);
}

class ZipContentSource : public IContentProvider
{
public:
    ZipContentSource(
        std::unique_ptr<Biz::Algorithms::ZipArchive>&& zip_archive
    ) :
        m_zip_archive(std::move(zip_archive))
    {}

    static std::unique_ptr<ZipContentSource> create(const std::string& zip_path)
    {
        // we need to capture the ZipArchive to unique_ptr so its address is fixed
        // this is required as the open_zip_reader's internals uses this address to set up
        // the structure
        std::unique_ptr<Algorithms::ZipArchive> zip_archive =
            std::make_unique<Algorithms::ZipArchive>(
                Algorithms::ZipArchive::open(zip_path, Algorithms::ZipArchive::Mode::Read)
            );
        const bool success = zip_archive->is_open();
        if (!success) {
            return nullptr;
        }
        return std::make_unique<ZipContentSource>(std::move(zip_archive));
    }

    FileList list_files() override
    {
        FileList file_list;
        const auto n = m_zip_archive->get_num_files();
        file_list.reserve(n);
        for (std::decay_t<decltype(n)> i = 0; i < n; i++) {
            std::string filename;
            if (m_zip_archive->get_filename(i, filename)) {
                file_list.emplace_back(std::move(filename));
            }
        }
        return file_list;
    }

    bool file_stream(
        const std::string& file_path,
        const StreamKernel& kernel,
        size_t buffer_size
    ) override
    {
        StreamKernel fwd = [&kernel](BytesView b) { kernel(b); };
        return mz_zip_reader_extract_file_to_callback(
            m_zip_archive->raw_archive(),
            file_path.c_str(),
            &ZipContentSource::pump_callback,
            &fwd,
            0
        );
    }

    const std::string& path() const override
    {
        return m_zip_archive->path();
    }


private:
    static size_t pump_callback(void *pOpaque, mz_uint64 file_ofs, const void *pBuf, size_t n)
    {
        const StreamKernel& kernel = *static_cast<const StreamKernel*>(pOpaque);
        kernel(BytesView{static_cast<const uint8_t*>(pBuf), n});
        return n;
    }

private:
    std::unique_ptr<Algorithms::ZipArchive> m_zip_archive;
};

IContentProviderPtr create_zip_source(const std::string& zip_path)
{
    return ZipContentSource::create(zip_path);
}

} // namespace Slic3r::Biz::Crypto
