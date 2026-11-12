#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/FileOpen/ArchiveEntry.hpp"
#include "Slic3r/Biz/FileOpen/FileOpeningInteractor.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/TestUtils/TestTempDir.hpp"

#include "ArchiveTestUtils.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>

using namespace Slic3r::Biz::FileOpen;
using Slic3r::Biz::FileOpen::Tests::make_3mf_bytes;
using Slic3r::Biz::FileOpen::Tests::read_file;
using Slic3r::Biz::FileOpen::Tests::write_file;
using Slic3r::Biz::FileOpen::Tests::write_zip;
namespace fs = boost::filesystem;

namespace {

EntryInfo entry(const std::string& name, std::uint64_t size = 100, std::uint64_t compressed = 50)
{
    EntryInfo info;
    info.index             = 0;
    info.name              = name;
    info.uncompressed_size = size;
    info.compressed_size   = compressed;
    return info;
}

const ArchiveFile* find_file(const ExtractedArchive& extracted, const std::string& name)
{
    auto it = std::find_if(extracted.files.begin(), extracted.files.end(), [&](const ArchiveFile& f) {
        return f.name == name;
    });
    return it == extracted.files.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("check_entry accepts supported files", "[FileOpen][ArchiveExtraction]")
{
    const ArchiveLimits limits;
    CHECK_FALSE(check_entry(entry("part.stl"), limits));
    CHECK_FALSE(check_entry(entry("models/sub/PART.STL"), limits));
    CHECK_FALSE(check_entry(entry("models\\part.obj"), limits));
    CHECK_FALSE(check_entry(entry("project.3mf"), limits));
    CHECK_FALSE(check_entry(entry("drawing.svg"), limits));
    CHECK_FALSE(check_entry(entry("cad.step"), limits));
    CHECK_FALSE(check_entry(entry("cad.stp"), limits));
    CHECK_FALSE(check_entry(entry("Žluťoučký kůň.stl"), limits));
}

TEST_CASE("check_entry rejects unsafe paths", "[FileOpen][ArchiveExtraction]")
{
    const ArchiveLimits limits;
    CHECK(check_entry(entry("../evil.stl"), limits) == SkipReason::UnsafePath);
    CHECK(check_entry(entry("models/../../evil.stl"), limits) == SkipReason::UnsafePath);
    CHECK(check_entry(entry("..\\evil.stl"), limits) == SkipReason::UnsafePath);
    CHECK(check_entry(entry("/etc/evil.stl"), limits) == SkipReason::UnsafePath);
    CHECK(check_entry(entry("C:/Windows/evil.stl"), limits) == SkipReason::UnsafePath);
    CHECK(check_entry(entry("\\\\server\\share\\evil.stl"), limits) == SkipReason::UnsafePath);
    CHECK(check_entry(entry("bad\x01name.stl"), limits) == SkipReason::InvalidName);
    CHECK(check_entry(entry(""), limits) == SkipReason::InvalidName);
}

TEST_CASE("check_entry rejects dangerous entries", "[FileOpen][ArchiveExtraction]")
{
    const ArchiveLimits limits;

    EntryInfo symlink = entry("link.stl");
    symlink.is_symlink = true;
    CHECK(check_entry(symlink, limits) == SkipReason::Symlink);

    EntryInfo encrypted = entry("secret.stl");
    encrypted.is_encrypted = true;
    CHECK(check_entry(encrypted, limits) == SkipReason::Encrypted);

    EntryInfo unsupported_method = entry("bzip2.stl");
    unsupported_method.is_supported_method = false;
    CHECK(check_entry(unsupported_method, limits) == SkipReason::UnsupportedCompression);

    CHECK(check_entry(entry("inner.zip"), limits) == SkipReason::NestedArchive);
    CHECK(check_entry(entry("inner.ZIP"), limits) == SkipReason::NestedArchive);
    CHECK(check_entry(entry("readme.txt"), limits) == SkipReason::Unsupported);
    CHECK(check_entry(entry("noextension"), limits) == SkipReason::Unsupported);
}

TEST_CASE("check_entry enforces size limits", "[FileOpen][ArchiveExtraction]")
{
    ArchiveLimits limits;
    limits.max_entry_size             = 1000;
    limits.max_compression_ratio      = 10;
    limits.compression_ratio_min_size = 100;

    CHECK_FALSE(check_entry(entry("ok.stl", 1000, 500), limits));
    CHECK(check_entry(entry("big.stl", 1001, 1001), limits) == SkipReason::TooLarge);
    CHECK(check_entry(entry("bomb.stl", 900, 10), limits) == SkipReason::SuspiciousCompressionRatio);
    CHECK(check_entry(entry("zero.stl", 900, 0), limits) == SkipReason::SuspiciousCompressionRatio);
    CHECK_FALSE(check_entry(entry("small.stl", 100, 1), limits));
}

TEST_CASE("entry_info_from_stat reads flags", "[FileOpen][ArchiveExtraction]")
{
    mz_zip_archive_file_stat stat;
    std::memset(&stat, 0, sizeof(stat));
    stat.m_file_index   = 7;
    stat.m_comp_size    = 10;
    stat.m_uncomp_size  = 20;
    stat.m_method       = MZ_DEFLATED;
    stat.m_is_supported = 1;

    SECTION("regular file") {
        stat.m_version_made_by = 3 << 8;
        stat.m_external_attr   = 0100644u << 16;
        const EntryInfo info   = entry_info_from_stat(stat, "part.stl");
        CHECK(info.index == 7);
        CHECK(info.name == "part.stl");
        CHECK(info.compressed_size == 10);
        CHECK(info.uncompressed_size == 20);
        CHECK_FALSE(info.is_symlink);
        CHECK_FALSE(info.is_encrypted);
        CHECK(info.is_supported_method);
    }
    SECTION("unix symlink") {
        stat.m_version_made_by = 3 << 8;
        stat.m_external_attr   = 0120777u << 16;
        CHECK(entry_info_from_stat(stat, "link.stl").is_symlink);
    }
    SECTION("symlink attribute from a DOS host is ignored") {
        stat.m_version_made_by = 0;
        stat.m_external_attr   = 0120777u << 16;
        CHECK_FALSE(entry_info_from_stat(stat, "link.stl").is_symlink);
    }
    SECTION("encrypted") {
        stat.m_bit_flag = 1;
        CHECK(entry_info_from_stat(stat, "secret.stl").is_encrypted);
    }
    SECTION("unsupported compression method") {
        stat.m_method = 12;
        CHECK_FALSE(entry_info_from_stat(stat, "part.stl").is_supported_method);
    }
    SECTION("invalid UTF-8 name") {
        CHECK(entry_info_from_stat(stat, "d\x8eklo.stl").name == "d_klo.stl");
    }
}

TEST_CASE("sanitized_file_name", "[FileOpen][ArchiveExtraction]")
{
    CHECK(sanitized_file_name("part.stl") == "part.stl");
    CHECK(sanitized_file_name("dir/sub/part.stl") == "part.stl");
    CHECK(sanitized_file_name("dir\\sub\\part.stl") == "part.stl");
    CHECK(sanitized_file_name("../../evil.stl") == "evil.stl");
    CHECK(sanitized_file_name("a<b>c|d?e*f\".stl") == "a_b_c_d_e_f_.stl");
    CHECK(sanitized_file_name("ctrl\x01.stl") == "ctrl_.stl");
    CHECK(sanitized_file_name("trailing. . ") == "trailing");
    CHECK(sanitized_file_name("con.stl") == "_con.stl");
    CHECK(sanitized_file_name("LPT1") == "_LPT1");
    CHECK(sanitized_file_name("") == "file");
    CHECK(sanitized_file_name("dir/") == "file");
    CHECK(sanitized_file_name("..") == "file");

    CHECK(sanitized_file_name("Žluťoučký kůň.stl") == "Žluťoučký kůň.stl");
    CHECK(sanitized_file_name("\xF0\x9F\x98\x80.stl") == "\xF0\x9F\x98\x80.stl");
    CHECK(sanitized_file_name("\x8e\x9a.stl") == "__.stl");
    CHECK(sanitized_file_name("\xC0\xAF.stl") == "__.stl");
    CHECK(sanitized_file_name("a\xC5") == "a_");
    CHECK(sanitized_file_name("\xED\xA0\x80") == "___");

    const std::string long_name = std::string(300, 'x') + ".stl";
    const std::string sanitized = sanitized_file_name(long_name);
    CHECK(sanitized.size() <= 200);
    CHECK(sanitized.substr(sanitized.size() - 4) == ".stl");

    std::string long_utf8;
    for (int i = 0; i < 150; ++i) {
        long_utf8 += "č";
    }
    const std::string sanitized_utf8 = sanitized_file_name(long_utf8 + ".stl");
    CHECK(sanitized_utf8.size() <= 200);
    CHECK(sanitized_file_name(sanitized_utf8) == sanitized_utf8);
}

TEST_CASE("extract_archive extracts openable files only", "[FileOpen][ArchiveExtraction]")
{
    ::Tests::TestTempDir temp;
    const auto archive = write_zip(
        temp.path() / "models.zip",
        {
            {"part.stl", "solid"},
            {"sub/model.obj", "v 0 0 0"},
            {"__MACOSX/._part.stl", "junk"},
            {".DS_Store", "junk"},
            {"models/Thumbs.db", "junk"},
            {"desktop.ini", "junk"},
            {"../evil.stl", "evil"},
            {"readme.txt", "text"},
            {"nested.zip", "zip"},
        }
    );

    const ExtractedArchive extracted = extract_archive(archive, {}, temp.path());
    CHECK(extracted.errors.empty());
    CHECK(extracted.files.size() == 5);

    const ArchiveFile* part = find_file(extracted, "part.stl");
    REQUIRE(part);
    CHECK_FALSE(part->skip_reason);
    CHECK(part->archive == archive);
    CHECK(part->size == 5);
    CHECK_FALSE(part->is_project);
    CHECK(read_file(part->path) == "solid");
    CHECK(part->path.parent_path().parent_path() == temp.path());

    const ArchiveFile* model = find_file(extracted, "sub/model.obj");
    REQUIRE(model);
    CHECK_FALSE(model->skip_reason);
    CHECK(model->path.filename() == "model.obj");
    CHECK(read_file(model->path) == "v 0 0 0");

    const ArchiveFile* evil = find_file(extracted, "../evil.stl");
    REQUIRE(evil);
    CHECK(evil->skip_reason == SkipReason::UnsafePath);
    CHECK(evil->path.empty());
    CHECK_FALSE(fs::exists(temp.path() / "evil.stl"));

    REQUIRE(find_file(extracted, "readme.txt"));
    CHECK(find_file(extracted, "readme.txt")->skip_reason == SkipReason::Unsupported);
    REQUIRE(find_file(extracted, "nested.zip"));
    CHECK(find_file(extracted, "nested.zip")->skip_reason == SkipReason::NestedArchive);
}

TEST_CASE("extract_archive gives unique names to files", "[FileOpen][ArchiveExtraction]")
{
    ::Tests::TestTempDir temp;
    const auto archive = write_zip(
        temp.path() / "models.zip",
        {{"deep/dir/part.stl", "first"}, {"other/PART.stl", "second"}, {"con.stl", "device"}}
    );

    const ExtractedArchive extracted = extract_archive(archive, {}, temp.path());
    REQUIRE(extracted.files.size() == 3);

    const fs::path directory = extracted.files[0].path.parent_path();
    CHECK(extracted.files[0].path == directory / "part.stl");
    CHECK(read_file(extracted.files[0].path) == "first");
    CHECK(extracted.files[1].path == directory / "PART (1).stl");
    CHECK(read_file(extracted.files[1].path) == "second");
    CHECK(extracted.files[2].path == directory / "_con.stl");
}

TEST_CASE("extract_archive tells projects from models", "[FileOpen][ArchiveExtraction]")
{
    ::Tests::TestTempDir temp;
    const auto archive = write_zip(
        temp.path() / "models.zip",
        {
            {"project.3mf", make_3mf_bytes(temp.path(), true)},
            {"geometry.3mf", make_3mf_bytes(temp.path(), false)},
            {"part.stl", "solid"},
        }
    );
    const auto legacy = write_zip(
        temp.path() / "legacy.3mf",
        {{"3D/3dmodel.model", "<model/>"}, {"Metadata/Slic3r_PE.config", "; config"}}
    );
    const auto with_legacy = write_zip(temp.path() / "with_legacy.zip", {{"legacy.3mf", read_file(legacy)}});

    const ExtractedArchive extracted = extract_archive(archive, {}, temp.path());
    REQUIRE(find_file(extracted, "project.3mf"));
    CHECK(find_file(extracted, "project.3mf")->is_project);
    CHECK(find_file(extracted, "project.3mf")->has_config);
    REQUIRE(find_file(extracted, "geometry.3mf"));
    CHECK(find_file(extracted, "geometry.3mf")->is_project);
    CHECK_FALSE(find_file(extracted, "geometry.3mf")->has_config);
    REQUIRE(find_file(extracted, "part.stl"));
    CHECK_FALSE(find_file(extracted, "part.stl")->is_project);
    CHECK_FALSE(find_file(extracted, "part.stl")->has_config);

    const ExtractedArchive extracted_legacy = extract_archive(with_legacy, {}, temp.path());
    REQUIRE(extracted_legacy.files.size() == 1);
    CHECK(extracted_legacy.files[0].is_project);
    CHECK(extracted_legacy.files[0].has_config);
}

TEST_CASE("extract_archive enforces archive limits", "[FileOpen][ArchiveExtraction]")
{
    ::Tests::TestTempDir temp;
    const auto archive = write_zip(
        temp.path() / "models.zip",
        {{"a.stl", std::string(100, 'a')}, {"b.stl", std::string(100, 'b')}, {"c.stl", std::string(10, 'c')}}
    );

    SECTION("too many entries") {
        ArchiveLimits limits;
        limits.max_entries = 2;
        const ExtractedArchive extracted = extract_archive(archive, limits, temp.path());
        CHECK(extracted.files.empty());
        CHECK_FALSE(extracted.errors.empty());
    }
    SECTION("total size") {
        ArchiveLimits limits;
        limits.max_total_size = 150;
        const ExtractedArchive extracted = extract_archive(archive, limits, temp.path());
        CHECK_FALSE(find_file(extracted, "a.stl")->skip_reason);
        CHECK(find_file(extracted, "b.stl")->skip_reason == SkipReason::ArchiveLimitsExceeded);
        CHECK(find_file(extracted, "b.stl")->path.empty());
        CHECK_FALSE(find_file(extracted, "c.stl")->skip_reason);
    }
}

TEST_CASE("extract_archive opens a renamed 3mf as one file", "[FileOpen][ArchiveExtraction]")
{
    ::Tests::TestTempDir temp;
    const auto archive = temp.path() / "project.zip";
    write_file(archive, make_3mf_bytes(temp.path(), true));

    const ExtractedArchive extracted = extract_archive(archive, {}, temp.path());
    CHECK(extracted.errors.empty());
    REQUIRE(extracted.files.size() == 1);
    CHECK_FALSE(extracted.files[0].skip_reason);
    CHECK(extracted.files[0].path.filename() == "project.3mf");
    CHECK(extracted.files[0].is_project);
    CHECK(extracted.files[0].has_config);
    CHECK(fs::exists(archive));
}

TEST_CASE("extract_archive reports invalid archives", "[FileOpen][ArchiveExtraction]")
{
    ::Tests::TestTempDir temp;
    const auto not_zip = temp.path() / "broken.zip";
    write_file(not_zip, "this is not a zip archive");

    const ExtractedArchive broken = extract_archive(not_zip, {}, temp.path());
    CHECK(broken.files.empty());
    CHECK_FALSE(broken.errors.empty());

    const ExtractedArchive missing = extract_archive(temp.path() / "missing.zip", {}, temp.path());
    CHECK(missing.files.empty());
    CHECK_FALSE(missing.errors.empty());
}

TEST_CASE("remove_stale_extractions removes old extractions only", "[FileOpen][ArchiveExtraction]")
{
    ::Tests::TestTempDir temp;
    const auto archive = write_zip(temp.path() / "models.zip", {{"part.stl", "solid"}});
    const fs::path stale  = extract_archive(archive, {}, temp.path()).files.at(0).path.parent_path();
    const fs::path recent = extract_archive(archive, {}, temp.path()).files.at(0).path.parent_path();
    const fs::path unrelated = temp.path() / "unrelated-old";
    fs::create_directory(unrelated);

    const std::time_t two_days_ago = std::time(nullptr) - 2 * 24 * 60 * 60;
    fs::last_write_time(stale, two_days_ago);
    fs::last_write_time(unrelated, two_days_ago);

    remove_stale_extractions(std::chrono::hours(24), temp.path());

    CHECK_FALSE(fs::exists(stale));
    CHECK(fs::exists(recent));
    CHECK(fs::exists(unrelated));
}
