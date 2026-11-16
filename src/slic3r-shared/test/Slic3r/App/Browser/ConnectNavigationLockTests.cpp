#include "Slic3r/App/Browser/ConnectNavigationLock.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using Slic3r::App::Browser::ConnectNavigationLock;

namespace {
const std::string SERVER       = "http://192.168.1.10:8080/";
const std::string LOADING      = "file:///C:/app/resources/web/connect_loading.html";
const std::string SIGNED_OUT   = "file:///C:/app/resources/web/connect_local_logged_out.html";
const std::string ERROR_PAGE   = "file:///C:/app/resources/web/connect_error_dark.html";
const std::string OTHER_SERVER = "http://192.168.1.11:8080/";
} // namespace

TEST_CASE("A tab that has not reached the Connect page lets everything through", "[connect-page]")
{
    ConnectNavigationLock lock(SERVER, "connect_loading");

    CHECK(lock.allows(LOADING, false));
    CHECK(lock.allows(SIGNED_OUT, false));
    CHECK(lock.allows(OTHER_SERVER, false));
    CHECK(lock.allows(SERVER + "printers/1", false));
    CHECK_FALSE(lock.locked());
}

TEST_CASE("Reaching the Connect page locks the tab to its service", "[connect-page]")
{
    ConnectNavigationLock lock(SERVER, "connect_loading");
    REQUIRE(lock.allows(SERVER, false));
    REQUIRE(lock.locked());

    CHECK(lock.allows(SERVER, false));
    CHECK(lock.allows(SERVER + "printers/1", false));
    CHECK(lock.allows("http://192.168.1.10:8080/pair?code=ABCD", false));
    CHECK_FALSE(lock.allows(OTHER_SERVER, false));
    CHECK_FALSE(lock.allows("http://192.168.1.10:8081/", false));
    CHECK_FALSE(lock.allows("https://192.168.1.10:8080/", false));
    CHECK_FALSE(lock.allows("http://192.168.1.10:8080.evil.example/", false));
}

TEST_CASE("The app's own pages pass a locked tab only where the tab says so", "[connect-page]")
{
    ConnectNavigationLock lock(SERVER, "connect_loading");
    REQUIRE(lock.allows(SERVER, false));

    CHECK_FALSE(lock.allows(SIGNED_OUT, false));
    CHECK_FALSE(lock.allows(ERROR_PAGE, false));

    CHECK(lock.allows(SIGNED_OUT, true));
    CHECK(lock.allows(ERROR_PAGE, true));
    CHECK(lock.allows(ERROR_PAGE, true));
    CHECK_FALSE(lock.allows("file:///C:/Users/me/secret.html", true));
}

TEST_CASE("A locked tab never returns to the loading page", "[connect-page]")
{
    ConnectNavigationLock lock(SERVER, "connect_loading");
    REQUIRE(lock.allows(SERVER, true));

    CHECK_FALSE(lock.allows(LOADING, true));
    CHECK_FALSE(lock.allows(LOADING, false));
}

TEST_CASE("The page of a failed connection passes a locked tab", "[connect-page]")
{
    ConnectNavigationLock lock(SERVER, "connect_loading");
    REQUIRE(lock.allows(SERVER, false));

    CHECK(lock.allows("file:///C:/app/resources/web/connection_failed.html", false));
}

TEST_CASE("A tab without a Connect page is never locked", "[connect-page]")
{
    ConnectNavigationLock lock(std::string(), "connect_loading");

    CHECK(lock.allows(std::string(), false));
    CHECK(lock.allows(OTHER_SERVER, false));
    CHECK_FALSE(lock.locked());
    CHECK_FALSE(lock.is_connect_url(OTHER_SERVER));
}
