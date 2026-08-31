#include "Slic3r/Biz/Algorithms/ZipArchive.hpp"

#include "Slic3r/Biz/Algorithms/MiniZWrapper.hpp"
#include "Slic3r/Utils.hpp"

#include <boost/nowide/fstream.hpp>

#include <algorithm>
#include <cstring>
#include <utility>

namespace Slic3r::Biz::Algorithms {

ZipArchive ZipArchive::open(const std::string& path, Mode mode)
{
    ZipArchive zip;
    zip.m_path    = path;
    zip.m_archive = std::make_unique<mz_zip_archive>();
    mz_zip_zero_struct(zip.m_archive.get());
    zip.m_mode = mode;
    zip.m_open = (mode == Mode::Read) ? open_zip_reader(zip.m_archive.get(), path) :
                                        open_zip_writer(zip.m_archive.get(), path);
    return zip;
}

ZipArchive::ZipArchive(ZipArchive&& other) noexcept :
    m_open(std::exchange(other.m_open, false)),
    m_mode(other.m_mode),
    m_path(std::move(other.m_path)),
    m_archive(std::move(other.m_archive)),
    m_compressor(std::exchange(other.m_compressor, nullptr)),
    m_compressor_level(other.m_compressor_level),
    m_decompressor(std::exchange(other.m_decompressor, nullptr))
{}

ZipArchive& ZipArchive::operator=(ZipArchive&& other) noexcept
{
    if (this == &other) {
        return *this;
    }
    close();
    if (m_compressor) {
        libdeflate_free_compressor(m_compressor);
    }
    if (m_decompressor) {
        libdeflate_free_decompressor(m_decompressor);
    }

    m_open             = std::exchange(other.m_open, false);
    m_mode             = other.m_mode;
    m_path             = std::move(other.m_path);
    m_archive          = std::move(other.m_archive);
    m_compressor       = std::exchange(other.m_compressor, nullptr);
    m_compressor_level = other.m_compressor_level;
    m_decompressor     = std::exchange(other.m_decompressor, nullptr);
    return *this;
}

ZipArchive::~ZipArchive()
{
    close();
    if (m_compressor) {
        libdeflate_free_compressor(m_compressor);
    }
    if (m_decompressor) {
        libdeflate_free_decompressor(m_decompressor);
    }
}

bool ZipArchive::is_open() const
{
    return m_open;
}

const std::string& ZipArchive::path() const
{
    return m_path;
}

bool ZipArchive::close()
{
    if (!m_open) {
        return true;
    }
    m_open = false;

    bool finalized = true;
    if (m_mode == Mode::Write && !finalize()) {
        finalized = false;
    }

    bool closed = (m_mode == Mode::Read) ? close_zip_reader(m_archive.get()) :
                                           close_zip_writer(m_archive.get());
    return finalized && closed;
}

int ZipArchive::locate_file(const std::string& filepath) const
{
    return mz_zip_reader_locate_file(m_archive.get(), filepath.c_str(), nullptr, 0);
}

bool ZipArchive::stat_file(const std::string& filepath, mz_zip_archive_file_stat& stat) const
{
    const int file_index = locate_file(filepath);
    if (file_index < 0) {
        return false;
    } else {
        return stat_file(file_index, stat);
    }
}

bool ZipArchive::stat_file(int file_index, mz_zip_archive_file_stat& stat) const
{
    return mz_zip_reader_file_stat(m_archive.get(), file_index, &stat);
}

int ZipArchive::get_num_files() const
{
    return mz_zip_reader_get_num_files(m_archive.get());
}

bool ZipArchive::get_filename(int file_index, std::string& filename) const
{
    // Read once to get filename size
    const mz_uint size = mz_zip_reader_get_filename(m_archive.get(), file_index, nullptr, 0);
    if (size == 0) {
        filename.clear();
        return false;
    }

    // size includes space for the terminating '\0'.
    filename.resize(size);
    const mz_uint written =
        mz_zip_reader_get_filename(m_archive.get(), file_index, filename.data(), size);

    if (written != size) {
        filename.clear();
        return false;
    }

    // Remove '\0' from std::string's logical size.
    filename.resize(size - 1);
    return true;
}

bool ZipArchive::extract_to_file(int file_index, const std::string& target_filename)
{
    mz_zip_archive_file_stat stat;
    if (!stat_file(file_index, stat)) {
        return false;
    }

    std::string data(static_cast<size_t>(stat.m_uncomp_size), '\0');
    if (!extract_entry_to(file_index, data.data(), data.size())) {
        return false;
    }

    boost::nowide::ofstream target_file(target_filename, std::ios::binary);
    if (!target_file.is_open()) {
        return false;
    }
    target_file.write(data.data(), data.size());
    return target_file.good();
}

bool ZipArchive::is_file_directory(int file_index) const
{
    return mz_zip_reader_is_file_a_directory(m_archive.get(), file_index);
}

mz_zip_archive* ZipArchive::raw_archive()
{
    return m_archive.get();
}

mz_zip_error ZipArchive::last_error() const
{
    return m_archive->m_last_error;
}

ZipArchive::Mode ZipArchive::mode() const
{
    return m_mode;
}

libdeflate_compressor* ZipArchive::compressor(int level)
{
    if (!m_compressor || m_compressor_level != level) {
        if (m_compressor) {
            libdeflate_free_compressor(m_compressor);
        }
        m_compressor       = libdeflate_alloc_compressor(level);
        m_compressor_level = level;
    }
    return m_compressor;
}

libdeflate_decompressor* ZipArchive::decompressor()
{
    if (!m_decompressor) {
        m_decompressor = libdeflate_alloc_decompressor();
    }
    return m_decompressor;
}

bool ZipArchive::inflate_and_verify(
    const std::string& raw,
    const mz_zip_archive_file_stat& stat,
    void* buf,
    size_t buf_size
)
{
    libdeflate_decompressor* d = decompressor();
    if (!d) {
        m_archive->m_last_error = MZ_ZIP_ALLOC_FAILED;
        return false;
    }

    if (libdeflate_deflate_decompress(d, raw.data(), raw.size(), buf, buf_size, nullptr)
        != LIBDEFLATE_SUCCESS)
    {
        m_archive->m_last_error = MZ_ZIP_DECOMPRESSION_FAILED;
        return false;
    }
    if (libdeflate_crc32(0, static_cast<const uint8_t*>(buf), buf_size) != stat.m_crc32) {
        m_archive->m_last_error = MZ_ZIP_CRC_CHECK_FAILED;
        return false;
    }
    return true;
}

bool ZipArchive::extract_entry_to(mz_uint file_index, void* buf, size_t buf_size)
{
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(m_archive.get(), file_index, &stat)) {
        return false;
    }
    if (buf_size != static_cast<size_t>(stat.m_uncomp_size)) {
        m_archive->m_last_error = MZ_ZIP_INVALID_PARAMETER;
        return false;
    }
    if (stat.m_method != MZ_DEFLATED) {
        // Stored entries, and any method miniz itself doesn't support decoding raw
        // (used only as a best-effort fallback for foreign archives), go through
        // miniz's own extraction unchanged.
        return mz_zip_reader_extract_to_mem(m_archive.get(), file_index, buf, buf_size, 0) != 0;
    }

    std::string raw(static_cast<size_t>(stat.m_comp_size), '\0');
    if (!mz_zip_reader_extract_to_mem(
            m_archive.get(),
            file_index,
            raw.data(),
            raw.size(),
            MZ_ZIP_FLAG_COMPRESSED_DATA
        ))
    {
        return false;
    } else {
        return inflate_and_verify(raw, stat, buf, buf_size);
    }
}

std::optional<ZipArchive::ExtractIter> ZipArchive::extract_iter(mz_uint file_index)
{
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(m_archive.get(), file_index, &stat)) {
        return std::nullopt;
    }

    ExtractIter it;
    it.m_data.resize(static_cast<size_t>(stat.m_uncomp_size));
    if (!it.m_data.empty() && !extract_entry_to(file_index, it.m_data.data(), it.m_data.size())) {
        return std::nullopt;
    } else {
        return it;
    }
}

size_t ZipArchive::ExtractIter::read(void* buf, size_t max_bytes)
{
    size_t remaining = m_data.size() - m_offset;
    size_t n         = std::min(remaining, max_bytes);
    if (n > 0) {
        std::memcpy(buf, m_data.data() + m_offset, n);
    }
    m_offset += n;
    return n;
}

bool ZipArchive::add_entry(const std::string& name, const void* data, size_t size, int level)
{
    libdeflate_compressor* c = compressor(level);
    if (!c) {
        m_archive->m_last_error = MZ_ZIP_ALLOC_FAILED;
        return false;
    }

    size_t bound = libdeflate_deflate_compress_bound(c, size);
    std::string compressed(bound, '\0');
    // Raw DEFLATE (not zlib/gzip-wrapped) -- what a MZ_ZIP_FLAG_COMPRESSED_DATA
    // entry with method MZ_DEFLATED expects.
    size_t compressed_size = libdeflate_deflate_compress(c, data, size, compressed.data(), bound);
    if (compressed_size == 0) {
        m_archive->m_last_error = MZ_ZIP_COMPRESSION_FAILED;
        return false;
    }
    compressed.resize(compressed_size);
    uint32_t crc = libdeflate_crc32(0, static_cast<const uint8_t*>(data), size);

    return mz_zip_writer_add_mem_ex_v2(
        m_archive.get(),
        name.c_str(),
        compressed.data(),
        compressed.size(),
        nullptr,
        0,
        MZ_ZIP_FLAG_COMPRESSED_DATA,
        size,
        crc,
        nullptr,
        nullptr,
        0,
        nullptr,
        0
    );
}

bool ZipArchive::add_entry_no_compression(const std::string& name, const void* data, size_t size)
{
    // level_and_flags = 0 (no compression, no MZ_ZIP_FLAG_COMPRESSED_DATA): miniz
    // auto-computes size/CRC and writes this entry with method "stored".
    return mz_zip_writer_add_mem_ex_v2(
        m_archive.get(),
        name.c_str(),
        data,
        size,
        nullptr,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        0,
        nullptr,
        0
    );
}

bool ZipArchive::add_entry_image(const std::string& name, const Domain::Image& thumbnail)
{
    size_t png_size = 0;
    void* png_data  = tdefl_write_image_to_png_file_in_memory_ex(
        thumbnail.pixels.data(),
        thumbnail.width(),
        thumbnail.height(),
        4,
        &png_size,
        MZ_DEFAULT_LEVEL,
        0
    );
    if (png_data == nullptr) {
        return false;
    }
    ScopeGuard sg_png_data([png_data]() { mz_free(png_data); });

    return add_entry(name, png_data, png_size);
}

bool ZipArchive::add_entry_file(const std::string& name, const std::string& filename, int level)
{
    boost::nowide::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    std::string data(static_cast<size_t>(file.tellg()), '\0');
    file.seekg(0);
    if (!data.empty() && !file.read(data.data(), data.size())) {
        return false;
    }

    return add_entry(name, data.data(), data.size(), level);
}

bool ZipArchive::finalize()
{
    return mz_zip_writer_finalize_archive(m_archive.get()) != 0;
}

} // namespace Slic3r::Biz::Algorithms
