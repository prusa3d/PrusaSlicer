#include <catch2/catch_test_macros.hpp>

#include <Slic3r/Biz/FileDownloader/FileDownloaderJob.hpp>

using namespace Slic3r::Biz::FileDownloader;

TEST_CASE("sanitize_filename keeps plain names")
{
    CHECK(sanitize_filename("model.3mf") == "model.3mf");
    CHECK(sanitize_filename(".hidden.stl") == ".hidden.stl");
    CHECK(sanitize_filename("\xc5\xa1koda.3mf") == "\xc5\xa1koda.3mf");
}

TEST_CASE("sanitize_filename strips path components")
{
    CHECK(sanitize_filename("../../evil.txt") == "evil.txt");
    CHECK(sanitize_filename("..\\..\\AppData\\evil.bat") == "evil.bat");
    CHECK(sanitize_filename("/etc/passwd") == "passwd");
    CHECK(sanitize_filename("C:\\Windows\\evil.dll") == "evil.dll");
    CHECK(sanitize_filename("\\\\server\\share\\evil.3mf") == "evil.3mf");
}

TEST_CASE("sanitize_filename removes colons and control characters")
{
    CHECK(sanitize_filename("C:evil.3mf") == "Cevil.3mf");
    CHECK(sanitize_filename("file.3mf:stream") == "file.3mfstream");
    CHECK(sanitize_filename("na\r\nme.3mf") == "name.3mf");
}

TEST_CASE("sanitize_filename rejects names without a usable file name")
{
    CHECK(sanitize_filename("").empty());
    CHECK(sanitize_filename(".").empty());
    CHECK(sanitize_filename("..").empty());
    CHECK(sanitize_filename("...").empty());
    CHECK(sanitize_filename(".. ").empty());
    CHECK(sanitize_filename("dir/").empty());
    CHECK(sanitize_filename("dir/..").empty());
}
