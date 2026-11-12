#include "Slic3r/Biz/Algorithms/ZipArchive.hpp"
#include "Slic3r/Biz/Algorithms/PNGReadWrite.hpp"
#include <catch2/catch_test_macros.hpp>
#include <boost/filesystem.hpp>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <vector>

using Slic3r::Biz::Algorithms::ZipArchive;
using Slic3r::Domain::Image;
using Slic3r::Domain::PixelFormat;

namespace {
std::string temp_zip_path()
{
    return (boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("ziparchive-test-%%%%.zip")).string();
}

std::string temp_file_path()
{
    return (boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("ziparchive-test-file-%%%%")).string();
}

// Bytes that would be corrupted by text-mode line-ending translation, to catch
// any regression to non-binary file I/O.
std::string binary_blob()
{
    std::string data(256, '\0');
    for (int i = 0; i < 256; ++i)
        data[i] = static_cast<char>(i);
    return data;
}
} // namespace

TEST_CASE("ZipArchive round trip: deflate and stored entries", "[algorithms][algorithms-zip-archive]") {
    const std::string path = temp_zip_path();
    const std::string deflatable(5000, 'a'); // highly compressible
    const std::string incompressible = binary_blob();

    {
        ZipArchive writer = ZipArchive::open(path, ZipArchive::Mode::Write);
        REQUIRE(writer.is_open());
        CHECK(writer.add_entry("deflated.txt", deflatable.data(), deflatable.size()));
        CHECK(writer.add_entry_no_compression("stored.bin", incompressible.data(), incompressible.size()));
    }

    ZipArchive reader = ZipArchive::open(path, ZipArchive::Mode::Read);
    REQUIRE(reader.is_open());

    int idx = reader.locate_file("deflated.txt");
    REQUIRE(idx >= 0);
    std::string out(deflatable.size(), '\0');
    CHECK(reader.extract_entry_to(idx, out.data(), out.size()));
    CHECK(out == deflatable);

    idx = reader.locate_file("stored.bin");
    REQUIRE(idx >= 0);
    out.assign(incompressible.size(), '\0');
    CHECK(reader.extract_entry_to(idx, out.data(), out.size()));
    CHECK(out == incompressible);

    reader.close();
    boost::filesystem::remove(path);
}

TEST_CASE("ZipArchive extract_iter yields the same bytes in chunks", "[algorithms][algorithms-zip-archive]") {
    const std::string path = temp_zip_path();
    const std::string content(10000, 'x');

    {
        ZipArchive writer = ZipArchive::open(path, ZipArchive::Mode::Write);
        REQUIRE(writer.is_open());
        CHECK(writer.add_entry("big.txt", content.data(), content.size()));
    }

    ZipArchive reader = ZipArchive::open(path, ZipArchive::Mode::Read);
    REQUIRE(reader.is_open());
    int idx = reader.locate_file("big.txt");
    REQUIRE(idx >= 0);

    auto it = reader.extract_iter(idx);
    REQUIRE(it.has_value());
    std::string out;
    char buf[777];
    size_t n;
    while ((n = it->read(buf, sizeof(buf))) > 0)
        out.append(buf, n);
    CHECK(out == content);

    reader.close();
    boost::filesystem::remove(path);
}

TEST_CASE("ZipArchive extract_entry_to rejects a mismatched buffer size", "[algorithms][algorithms-zip-archive]") {
    const std::string path = temp_zip_path();
    const std::string content = "hello world";

    {
        ZipArchive writer = ZipArchive::open(path, ZipArchive::Mode::Write);
        REQUIRE(writer.is_open());
        CHECK(writer.add_entry("f.txt", content.data(), content.size()));
    }

    ZipArchive reader = ZipArchive::open(path, ZipArchive::Mode::Read);
    REQUIRE(reader.is_open());
    int idx = reader.locate_file("f.txt");
    REQUIRE(idx >= 0);

    std::string out(content.size() - 1, '\0');
    CHECK_FALSE(reader.extract_entry_to(idx, out.data(), out.size()));

    reader.close();
    boost::filesystem::remove(path);
}

TEST_CASE("ZipArchive move preserves path, mode, and open state", "[algorithms][algorithms-zip-archive]") {
    const std::string path = temp_zip_path();
    ZipArchive a = ZipArchive::open(path, ZipArchive::Mode::Write);
    REQUIRE(a.is_open());

    ZipArchive b = std::move(a); // move construction
    CHECK(b.is_open());
    CHECK(b.path() == path);
    CHECK(b.mode() == ZipArchive::Mode::Write);

    ZipArchive c = ZipArchive::open(temp_zip_path(), ZipArchive::Mode::Read);
    c = std::move(b); // move assignment
    CHECK(c.is_open());
    CHECK(c.path() == path);
    CHECK(c.mode() == ZipArchive::Mode::Write);

    CHECK(c.add_entry("f.txt", "x", 1));
    c.close();
    boost::filesystem::remove(path);
}

TEST_CASE("ZipArchive close() is idempotent and produces a valid archive", "[algorithms][algorithms-zip-archive]") {
    const std::string path = temp_zip_path();
    ZipArchive writer = ZipArchive::open(path, ZipArchive::Mode::Write);
    REQUIRE(writer.is_open());
    CHECK(writer.add_entry("f.txt", "hello", 5));

    CHECK(writer.close());
    CHECK(writer.close()); // redundant close must stay a harmless no-op, not re-finalize
    CHECK_FALSE(writer.is_open());

    ZipArchive reader = ZipArchive::open(path, ZipArchive::Mode::Read);
    REQUIRE(reader.is_open());
    int idx = reader.locate_file("f.txt");
    REQUIRE(idx >= 0);
    std::string out(5, '\0');
    CHECK(reader.extract_entry_to(idx, out.data(), out.size()));
    CHECK(out == "hello");

    reader.close();
    boost::filesystem::remove(path);
}

TEST_CASE("ZipArchive stat_file reports entry metadata and rejects missing entries", "[algorithms][algorithms-zip-archive]") {
    const std::string path = temp_zip_path();
    const std::string deflatable(2000, 'a');
    const std::string stored = "stored-bytes";

    {
        ZipArchive writer = ZipArchive::open(path, ZipArchive::Mode::Write);
        REQUIRE(writer.is_open());
        CHECK(writer.add_entry("deflated.txt", deflatable.data(), deflatable.size()));
        CHECK(writer.add_entry_no_compression("stored.bin", stored.data(), stored.size()));
    }

    ZipArchive reader = ZipArchive::open(path, ZipArchive::Mode::Read);
    REQUIRE(reader.is_open());

    mz_zip_archive_file_stat stat;
    REQUIRE(reader.stat_file("deflated.txt", stat));
    CHECK(stat.m_uncomp_size == deflatable.size());
    CHECK(stat.m_method == MZ_DEFLATED);

    REQUIRE(reader.stat_file("stored.bin", stat));
    CHECK(stat.m_uncomp_size == stored.size());
    CHECK(stat.m_method == 0);

    const int idx = reader.locate_file("stored.bin");
    REQUIRE(idx >= 0);
    mz_zip_archive_file_stat stat_by_index;
    REQUIRE(reader.stat_file(idx, stat_by_index));
    CHECK(stat_by_index.m_uncomp_size == stat.m_uncomp_size);

    CHECK_FALSE(reader.stat_file("does-not-exist.txt", stat));
    CHECK_FALSE(reader.stat_file(reader.get_num_files() + 1, stat));

    reader.close();
    boost::filesystem::remove(path);
}

TEST_CASE("ZipArchive get_num_files and get_filename round trip entry names", "[algorithms][algorithms-zip-archive]") {
    const std::string path = temp_zip_path();
    const std::vector<std::string> names = {"a.txt", "dir/b.txt", "c.bin"};

    {
        ZipArchive writer = ZipArchive::open(path, ZipArchive::Mode::Write);
        REQUIRE(writer.is_open());
        for (const std::string& name : names)
            CHECK(writer.add_entry(name, "x", 1));
    }

    ZipArchive reader = ZipArchive::open(path, ZipArchive::Mode::Read);
    REQUIRE(reader.is_open());
    REQUIRE(reader.get_num_files() == static_cast<int>(names.size()));

    std::vector<std::string> read_names;
    for (int i = 0; i < reader.get_num_files(); ++i) {
        std::string filename;
        REQUIRE(reader.get_filename(i, filename));
        read_names.push_back(filename);
    }
    CHECK(std::is_permutation(read_names.begin(), read_names.end(), names.begin()));

    reader.close();
    boost::filesystem::remove(path);
}

TEST_CASE("ZipArchive is_file_directory distinguishes directories from files", "[algorithms][algorithms-zip-archive]") {
    const std::string path = temp_zip_path();

    {
        ZipArchive writer = ZipArchive::open(path, ZipArchive::Mode::Write);
        REQUIRE(writer.is_open());
        CHECK(writer.add_entry_no_compression("dir/", nullptr, 0));
        CHECK(writer.add_entry("dir/file.txt", "x", 1));
    }

    ZipArchive reader = ZipArchive::open(path, ZipArchive::Mode::Read);
    REQUIRE(reader.is_open());

    int dir_idx = reader.locate_file("dir/");
    REQUIRE(dir_idx >= 0);
    CHECK(reader.is_file_directory(dir_idx));

    int file_idx = reader.locate_file("dir/file.txt");
    REQUIRE(file_idx >= 0);
    CHECK_FALSE(reader.is_file_directory(file_idx));

    reader.close();
    boost::filesystem::remove(path);
}

TEST_CASE("ZipArchive extract_to_file is binary-safe and reports failures", "[algorithms][algorithms-zip-archive]") {
    const std::string zip_path = temp_zip_path();
    const std::string data = binary_blob();

    {
        ZipArchive writer = ZipArchive::open(zip_path, ZipArchive::Mode::Write);
        REQUIRE(writer.is_open());
        CHECK(writer.add_entry("f.bin", data.data(), data.size()));
    }

    ZipArchive reader = ZipArchive::open(zip_path, ZipArchive::Mode::Read);
    REQUIRE(reader.is_open());
    int idx = reader.locate_file("f.bin");
    REQUIRE(idx >= 0);

    const std::string out_path = temp_file_path();
    REQUIRE(reader.extract_to_file(idx, out_path));

    std::ifstream check(out_path, std::ios::binary);
    REQUIRE(check.is_open());
    std::string read_back((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());
    CHECK(read_back == data);

    // Target directory doesn't exist -> the ofstream can't be opened.
    CHECK_FALSE(reader.extract_to_file(idx, (boost::filesystem::path(out_path) / "no" / "such" / "dir" / "f").string()));

    reader.close();
    check.close();
    boost::filesystem::remove(zip_path);
    boost::filesystem::remove(out_path);
}

TEST_CASE("ZipArchive add_entry_file round trips binary file contents", "[algorithms][algorithms-zip-archive]") {
    const std::string src_path = temp_file_path();
    const std::string data = binary_blob();
    {
        std::ofstream src(src_path, std::ios::binary);
        REQUIRE(src.is_open());
        src.write(data.data(), data.size());
    }

    const std::string zip_path = temp_zip_path();
    {
        ZipArchive writer = ZipArchive::open(zip_path, ZipArchive::Mode::Write);
        REQUIRE(writer.is_open());
        CHECK(writer.add_entry_file("f.bin", src_path));
        // A missing source file must fail rather than silently add an empty entry.
        CHECK_FALSE(writer.add_entry_file("missing.bin", src_path + "-does-not-exist"));
    }

    ZipArchive reader = ZipArchive::open(zip_path, ZipArchive::Mode::Read);
    REQUIRE(reader.is_open());
    int idx = reader.locate_file("f.bin");
    REQUIRE(idx >= 0);
    std::string out(data.size(), '\0');
    CHECK(reader.extract_entry_to(idx, out.data(), out.size()));
    CHECK(out == data);
    CHECK(reader.locate_file("missing.bin") < 0);

    reader.close();
    boost::filesystem::remove(src_path);
    boost::filesystem::remove(zip_path);
}

TEST_CASE("ZipArchive add_entry_image round trips a PNG thumbnail", "[algorithms][algorithms-zip-archive]") {
    const std::string path = temp_zip_path();
    Image thumbnail(PixelFormat::RGBA8, 4, 4);
    for (size_t i = 0; i < thumbnail.pixels.size(); ++i)
        thumbnail.pixels[i] = static_cast<uint8_t>(i);

    {
        ZipArchive writer = ZipArchive::open(path, ZipArchive::Mode::Write);
        REQUIRE(writer.is_open());
        CHECK(writer.add_entry_image("thumbnail.png", thumbnail));
    }

    ZipArchive reader = ZipArchive::open(path, ZipArchive::Mode::Read);
    REQUIRE(reader.is_open());
    mz_zip_archive_file_stat stat;
    REQUIRE(reader.stat_file("thumbnail.png", stat));
    std::string png_bytes(stat.m_uncomp_size, '\0');
    REQUIRE(reader.extract_entry_to(stat.m_file_index, png_bytes.data(), png_bytes.size()));

    std::vector<unsigned char> pixels;
    unsigned width = 0, height = 0;
    REQUIRE(Slic3r::png::decode_png(png_bytes, pixels, width, height));
    CHECK(width == 4);
    CHECK(height == 4);
    CHECK(pixels.size() == thumbnail.pixels.size());

    reader.close();
    boost::filesystem::remove(path);
}

TEST_CASE("ZipArchive raw_archive is non-null while open", "[algorithms][algorithms-zip-archive]") {
    const std::string path = temp_zip_path();
    ZipArchive writer = ZipArchive::open(path, ZipArchive::Mode::Write);
    REQUIRE(writer.is_open());
    CHECK(writer.raw_archive() != nullptr);
    writer.close();
    boost::filesystem::remove(path);
}

TEST_CASE("ZipArchive last_error reports why opening a missing file failed", "[algorithms][algorithms-zip-archive]") {
    ZipArchive reader = ZipArchive::open(temp_zip_path(), ZipArchive::Mode::Read);
    CHECK_FALSE(reader.is_open());
    CHECK(reader.last_error() != MZ_ZIP_NO_ERROR);
}

TEST_CASE("ZipArchive get_filename_from_extra prefers the Unicode Path extra field", "[algorithms][algorithms-zip-archive]") {
    const std::string path = temp_zip_path();
    const std::string legacy_name = "d\x8eklo.stl";
    const std::string utf8_name   = "d\xC5\xBEklo.stl";

    std::string extra;
    const auto put_u16 = [&extra](uint16_t v) {
        extra.push_back(static_cast<char>(v & 0xff));
        extra.push_back(static_cast<char>(v >> 8));
    };
    put_u16(0x7075);
    put_u16(static_cast<uint16_t>(5 + utf8_name.size()));
    extra.push_back('\x01');
    const uint32_t crc = static_cast<uint32_t>(mz_crc32(
        MZ_CRC32_INIT,
        reinterpret_cast<const unsigned char*>(legacy_name.data()),
        legacy_name.size()
    ));
    for (int shift = 0; shift < 32; shift += 8)
        extra.push_back(static_cast<char>((crc >> shift) & 0xff));
    extra += utf8_name;

    {
        ZipArchive writer = ZipArchive::open(path, ZipArchive::Mode::Write);
        REQUIRE(writer.is_open());
        REQUIRE(mz_zip_writer_add_mem_ex_v2(
            writer.raw_archive(),
            legacy_name.c_str(),
            "x",
            1,
            nullptr,
            0,
            0,
            0,
            0,
            nullptr,
            extra.data(),
            static_cast<mz_uint>(extra.size()),
            extra.data(),
            static_cast<mz_uint>(extra.size())
        ));
        CHECK(writer.add_entry("plain.stl", "x", 1));
    }

    ZipArchive reader = ZipArchive::open(path, ZipArchive::Mode::Read);
    REQUIRE(reader.is_open());

    std::string filename;
    REQUIRE(reader.get_filename_from_extra(reader.locate_file(legacy_name), filename));
    CHECK(filename == utf8_name);
    REQUIRE(reader.get_filename(reader.locate_file(legacy_name), filename));
    CHECK(filename == legacy_name);

    REQUIRE(reader.get_filename_from_extra(reader.locate_file("plain.stl"), filename));
    CHECK(filename == "plain.stl");

    CHECK_FALSE(reader.get_filename_from_extra(-1, filename));

    reader.close();
    boost::filesystem::remove(path);
}
