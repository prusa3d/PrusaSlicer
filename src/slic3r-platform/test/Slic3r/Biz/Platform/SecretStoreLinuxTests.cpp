#include "Slic3r/Biz/Platform/SecretStoreLinux.hpp"

#include "Slic3r/Directories.hpp"

#include <boost/filesystem.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {

struct SecretStoreLinuxFixture
{
    SecretStoreLinuxFixture() :
        previous_data_dir(Slic3r::data_dir()),
        directory(
            boost::filesystem::temp_directory_path()
            / boost::filesystem::unique_path("slic3r-secret-store-%%%%-%%%%-%%%%")
        )
    {
        boost::filesystem::create_directories(directory / "shared_runtime");
        Slic3r::set_data_dir(directory.string());
    }

    ~SecretStoreLinuxFixture()
    {
        Slic3r::set_data_dir(previous_data_dir);
        boost::system::error_code ec;
        boost::filesystem::remove_all(directory, ec);
    }

    std::string previous_data_dir;
    boost::filesystem::path directory;
    Slic3r::Biz::Platform::SecretStoreLinux store;
};

} // namespace

TEST_CASE_METHOD(
    SecretStoreLinuxFixture,
    "A saved secret is loaded back",
    "[secret_store]"
)
{
    REQUIRE(store.save_secret("Service", "tokens", "access|refresh|key|123|456"));

    std::string user;
    std::string secret;
    REQUIRE(store.load_secret("Service", user, secret));
    CHECK(user == "tokens");
    CHECK(secret == "access|refresh|key|123|456");
}

TEST_CASE_METHOD(
    SecretStoreLinuxFixture,
    "A shorter secret replaces a longer one completely",
    "[secret_store]"
)
{
    REQUIRE(store.save_secret("Service", "tokens", std::string(500, 'a') + "|id|1|http://server"));
    REQUIRE(store.save_secret("Service", "tokens", "short|id|1|http://s"));

    std::string user;
    std::string secret;
    REQUIRE(store.load_secret("Service", user, secret));
    CHECK(user == "tokens");
    CHECK(secret == "short|id|1|http://s");
}

TEST_CASE_METHOD(
    SecretStoreLinuxFixture,
    "An empty secret clears the stored one",
    "[secret_store]"
)
{
    REQUIRE(store.save_secret("Service", "tokens", "token|id|1|http://server"));
    REQUIRE(store.save_secret("Service", "tokens", ""));

    std::string user;
    std::string secret = "untouched";
    REQUIRE(store.load_secret("Service", user, secret));
    CHECK(user == "tokens");
    CHECK(secret.empty());
}

TEST_CASE_METHOD(
    SecretStoreLinuxFixture,
    "A secret that was never saved cannot be loaded",
    "[secret_store]"
)
{
    std::string user;
    std::string secret;
    CHECK_FALSE(store.load_secret("Missing", user, secret));
}

TEST_CASE_METHOD(
    SecretStoreLinuxFixture,
    "Secrets of different services are kept apart",
    "[secret_store]"
)
{
    REQUIRE(store.save_secret("First", "tokens", "first-secret"));
    REQUIRE(store.save_secret("Second", "tokens", "second"));
    REQUIRE(store.save_secret("First", "tokens", ""));

    std::string user;
    std::string secret;
    REQUIRE(store.load_secret("Second", user, secret));
    CHECK(secret == "second");
}
