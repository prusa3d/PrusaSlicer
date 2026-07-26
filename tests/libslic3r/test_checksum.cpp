#include <catch2/catch_test_macros.hpp>
#include "libslic3r/libslic3r.h"

#include <atomic>
#include <string>

#include <boost/crc.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include "libslic3r/Checksum.hpp"

using namespace Slic3r;

namespace {

// Writes the given bytes into a uniquely named file in the temp directory and removes it again
// when it goes out of scope.
class ScopedTempFile
{
public:
    explicit ScopedTempFile(const std::string &content)
    {
        static std::atomic<int> counter{0};
        m_path = boost::filesystem::temp_directory_path()
            / ("slic3r-checksum-test-" + std::to_string(counter.fetch_add(1)) + ".bin");

        boost::nowide::ofstream file(m_path.string(), std::ios::out | std::ios::binary);
        file.write(content.data(), content.size());
        file.close();
    }
    ~ScopedTempFile() { boost::system::error_code ec; boost::filesystem::remove(m_path, ec); }

    ScopedTempFile(const ScopedTempFile &) = delete;
    ScopedTempFile& operator=(const ScopedTempFile &) = delete;

    const boost::filesystem::path& path() const { return m_path; }

private:
    boost::filesystem::path m_path;
};

} // namespace

TEST_CASE("Hexadecimal formatting of a CRC32", "[Checksum]") {
    // rr_upload expects the value zero padded to 8 digits and without the "0x" prefix. A shorter
    // string would be parsed by RepRapFirmware as a different number and every upload would fail.
    REQUIRE(checksum::to_hex(0xcbf43926) == "cbf43926");
    REQUIRE(checksum::to_hex(0x000000ff) == "000000ff");
    REQUIRE(checksum::to_hex(0x00000000) == "00000000");
    REQUIRE(checksum::to_hex(0xffffffff) == "ffffffff");
    // Must be lower case hexadecimal.
    REQUIRE(checksum::to_hex(0xdeadbeef) == "deadbeef");
}

TEST_CASE("CRC32 of a file matches the standard CRC-32/ISO-HDLC check value", "[Checksum]") {
    // "123456789" -> 0xCBF43926 is the check value of the CRC-32 variant used by zlib and by
    // RepRapFirmware. This is what pins the implementation to the variant the firmware expects,
    // as opposed to for example CRC-32C (Castagnoli), which would yield 0xE3069283.
    const ScopedTempFile file("123456789");
    const std::optional<uint32_t> crc = checksum::crc32_file(file.path());
    REQUIRE(crc.has_value());
    REQUIRE(*crc == 0xcbf43926u);
    REQUIRE(checksum::to_hex(*crc) == "cbf43926");
}

TEST_CASE("CRC32 of an empty file is zero", "[Checksum]") {
    const ScopedTempFile file("");
    const std::optional<uint32_t> crc = checksum::crc32_file(file.path());
    REQUIRE(crc.has_value());
    REQUIRE(*crc == 0x00000000u);
}

TEST_CASE("CRC32 is correct across the read buffer boundary", "[Checksum]") {
    // The implementation reads in 64 KiB chunks and stops on a short read. Sizes that are an exact
    // multiple of the buffer are the risky ones: there the last full read succeeds without setting
    // eofbit, so a naive loop either drops the final chunk or counts it twice.
    constexpr size_t buffer_size = 64 * 1024;
    const size_t sizes[] = { buffer_size - 1, buffer_size, buffer_size + 1,
                             2 * buffer_size, 2 * buffer_size + 1, 3 * buffer_size, 200 * 1024 };

    for (size_t size : sizes) {
        std::string content;
        content.reserve(size);
        for (size_t i = 0; i < size; ++ i)
            content.push_back(static_cast<char>(i * 31 + (i >> 8)));

        const ScopedTempFile file(content);
        const std::optional<uint32_t> crc = checksum::crc32_file(file.path());
        REQUIRE(crc.has_value());

        // Cross check against a single shot CRC over the same bytes.
        boost::crc_32_type reference;
        reference.process_bytes(content.data(), content.size());
        INFO("file size " << size);
        REQUIRE(*crc == reference.checksum());
    }
}

TEST_CASE("CRC32 is computed over the raw bytes, including CR LF", "[Checksum]") {
    // Regression guard: the file must be read in binary mode. If it were opened in text mode, a
    // Windows build would collapse "\r\n" to "\n" and produce a checksum that does not match the
    // bytes actually uploaded.
    const std::string content = "G1 X1 Y1\r\nG1 X2 Y2\r\n";
    const ScopedTempFile file(content);
    const std::optional<uint32_t> crc = checksum::crc32_file(file.path());
    REQUIRE(crc.has_value());

    boost::crc_32_type reference;
    reference.process_bytes(content.data(), content.size());
    REQUIRE(*crc == reference.checksum());
}

TEST_CASE("CRC32 of a missing file reports failure instead of a bogus value", "[Checksum]") {
    const boost::filesystem::path missing =
        boost::filesystem::temp_directory_path() / "slic3r-checksum-this-file-does-not-exist.bin";
    REQUIRE(! boost::filesystem::exists(missing));
    REQUIRE(! checksum::crc32_file(missing).has_value());
}
