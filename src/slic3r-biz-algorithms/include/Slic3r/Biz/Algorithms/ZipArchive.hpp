#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include <miniz.h>
#include <libdeflate.h>

#include <Slic3r/Domain/Image.hpp>

namespace Slic3r::Biz::Algorithms {

/**
 * @brief The ZipArchive class
 * Zip archive reader/writer. Uses miniz for the container format (central directory, zip64, local headers)
 * and libdeflate for the DEFLATE codec itself, via miniz's raw MZ_ZIP_FLAG_COMPRESSED_DATA passthrough.
 * miniz never runs its own (de)compressor.
 */
class ZipArchive
{
public:
    enum class Mode
    {
        Read,
        Write
    };

    /**
     * @note check is_open() before use. On failure last_error() still reports why
     */
    static ZipArchive open(const std::string& path, Mode mode);

    ZipArchive(const ZipArchive&)            = delete;
    ZipArchive& operator=(const ZipArchive&) = delete;
    ZipArchive(ZipArchive&& other) noexcept;
    ZipArchive& operator=(ZipArchive&& other) noexcept;
    ~ZipArchive();

    bool is_open() const;
    const std::string& path() const;

    bool close();
    Mode mode() const;

    int locate_file(const std::string& filepath) const;
    bool stat_file(const std::string& filepath, mz_zip_archive_file_stat& stat) const;
    bool stat_file(int file_index, mz_zip_archive_file_stat& stat) const;
    int get_num_files() const;
    bool get_filename(int file_index, std::string& filename) const;
    /**
     * @brief UTF-8 name from the Info-ZIP Unicode Path extra field, get_filename() when missing.
     */
    bool get_filename_from_extra(int file_index, std::string& filename) const;
    bool extract_to_file(int file_index, const std::string& target_filename);
    bool is_file_directory(int file_index) const;

    /**
     * @warning Only use as last resort, e.g. for streaming when there is no
     * libdeflate atlernative
     */
    mz_zip_archive* raw_archive();

    mz_zip_error last_error() const;

    /**
     * @brief Decompresses (or copies, for stored entries) the whole entry into buf.
     * @note buf_size must equal the entry's stat.m_uncomp_size.
     */
    bool extract_entry_to(mz_uint file_index, void* buf, size_t buf_size);

    // Pull-style reader over one fully-decompressed entry, mirroring
    // mz_zip_reader_extract_iter_new/_read. libdeflate has no incremental
    // decompression API, so the whole entry is decompressed up front on
    // construction; read() then just slices it out in caller-sized chunks
    class ExtractIter
    {
    public:
        size_t read(void* buf, size_t max_bytes);

    private:
        friend class ZipArchive;
        std::string m_data;
        size_t m_offset = 0;
    };

    std::optional<ExtractIter> extract_iter(mz_uint file_index);

    /**
     * @warning level is libdeflate level (0-12, 12 = best), not miniz (0-9, 9 = best)
     */
    bool add_entry(const std::string& name, const void* data, size_t size, int level = 6);
    bool add_entry_no_compression(const std::string& name, const void* data, size_t size);
    /**
     * @note uses miniz compression
     */
    bool add_entry_image(const std::string& name, const Domain::Image& thumbnail);
    /**
     * @warning level is libdeflate level (0-12, 12 = best), not miniz (0-9, 9 = best)
     */
    bool add_entry_file(const std::string& name, const std::string& filename, int level = 6);

private:
    ZipArchive() = default;

    bool finalize();

    bool inflate_and_verify(
        const std::string& raw,
        const mz_zip_archive_file_stat& stat,
        void* buf,
        size_t buf_size
    );
    libdeflate_compressor* compressor(int level);
    libdeflate_decompressor* decompressor();

private:
    bool m_open = false;
    Mode m_mode = Mode::Read;
    std::string m_path;
    std::unique_ptr<mz_zip_archive> m_archive;
    libdeflate_compressor* m_compressor     = nullptr;
    int m_compressor_level                  = -1;
    libdeflate_decompressor* m_decompressor = nullptr;
};

} // namespace Slic3r::Biz::Algorithms
