#include "Slic3r/Biz/Crypto/ContentProvider.hpp"
#include "Slic3r/Biz/Algorithms/MiniZWrapper.hpp"

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
        std::unique_ptr<Biz::Algorithms::MZ_Archive>&& zip_archive,
        std::string  zip_path
    ) :
        m_zip_archive(std::move(zip_archive)),
        m_zip_path(std::move(zip_path))
    {}

    ~ZipContentSource() override
    {
        Algorithms::close_zip_reader(&m_zip_archive->arch);
    }

    static std::unique_ptr<ZipContentSource> create(const std::string& zip_path)
    {
        // we need to capture the MZ_Archive to unique_ptr so its address is fixed
        // this is required as the open_zip_reader's internals uses this address to set up
        // the structure
        auto zip_archive = std::make_unique<Algorithms::MZ_Archive>();
        const bool success = Algorithms::open_zip_reader(&zip_archive->arch, zip_path);
        if (!success) {
            return nullptr;
        }
        return std::make_unique<ZipContentSource>(std::move(zip_archive), zip_path);
    }

    FileList list_files() override
    {
        FileList file_list;
        const auto n = mz_zip_reader_get_num_files(&m_zip_archive->arch);
        file_list.reserve(n);
        for (std::decay_t<decltype(n)> i = 0; i < n; i++) {
            constexpr size_t BUF_SIZE = 256;
            char file_name_buf[BUF_SIZE];
            if (mz_zip_reader_get_filename(&m_zip_archive->arch, i, file_name_buf, BUF_SIZE) > 0) {
                file_list.emplace_back(file_name_buf);
            }
        }
        return file_list;
    }

    bool file_stream(const std::string& file_path, const StreamKernel& kernel, size_t buffer_size) override
    {
        StreamKernel fwd = [&kernel](BytesView b) { kernel(b); };
        return mz_zip_reader_extract_file_to_callback(
            &m_zip_archive->arch,
            file_path.c_str(),
            &ZipContentSource::pump_callback,
            &fwd,
            0
        );
    }

    const std::string& path() const override
    {
        return m_zip_path;
    }


private:
    static size_t pump_callback(void *pOpaque, mz_uint64 file_ofs, const void *pBuf, size_t n)
    {
        const StreamKernel& kernel = *static_cast<const StreamKernel*>(pOpaque);
        kernel(BytesView{static_cast<const uint8_t*>(pBuf), n});
        return n;
    }

private:
    std::unique_ptr<Algorithms::MZ_Archive> m_zip_archive;
    std::string m_zip_path;
};

IContentProviderPtr create_zip_source(const std::string& zip_path)
{
    return ZipContentSource::create(zip_path);
}

} // namespace Slic3r::Biz::Crypto
