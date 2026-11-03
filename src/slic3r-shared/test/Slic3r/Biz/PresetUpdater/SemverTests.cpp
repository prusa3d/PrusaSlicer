#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/PresetUpdater/PresetUpdaterIndex.hpp"


TEST_CASE("Check parsing and comparing of config versions", "[Version]") {

    Slic3r::Biz::PresetUpdater::Version v;
    using Slic3r::Semver;

    v.config_version     = *Semver::parse("1.1.2");
    v.min_slic3r_version = *Semver::parse("1.38.0");
    v.max_slic3r_version = Semver::inf();
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.38.0")));
    REQUIRE(! v.is_slic3r_supported(*Semver::parse("1.38.0-alpha")));
    REQUIRE(! v.is_slic3r_supported(*Semver::parse("1.37.0-alpha")));

    // Test the prerelease status.
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha1")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha1")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-beta")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-rc2")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0")));

    v.config_version      = *Semver::parse("1.1.2-alpha");
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha1")));
    REQUIRE(! v.is_slic3r_supported(*Semver::parse("1.39.0-beta")));
    REQUIRE(! v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
    REQUIRE(! v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
    REQUIRE(! v.is_slic3r_supported(*Semver::parse("1.39.0-rc2")));
    REQUIRE(! v.is_slic3r_supported(*Semver::parse("1.39.0")));

    v.config_version      = *Semver::parse("1.1.2-alpha1");
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha1")));
    REQUIRE(! v.is_slic3r_supported(*Semver::parse("1.39.0-beta")));
    REQUIRE(! v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
    REQUIRE(! v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
    REQUIRE(! v.is_slic3r_supported(*Semver::parse("1.39.0-rc2")));
    REQUIRE(! v.is_slic3r_supported(*Semver::parse("1.39.0")));

    v.config_version      = *Semver::parse("1.1.2-beta");
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha1")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-beta")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
    REQUIRE(! v.is_slic3r_supported(*Semver::parse("1.39.0-rc")));
    REQUIRE(! v.is_slic3r_supported(*Semver::parse("1.39.0-rc2")));
    REQUIRE(! v.is_slic3r_supported(*Semver::parse("1.39.0")));

    v.config_version      = *Semver::parse("1.1.2-rc");
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha1")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-beta")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-rc")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-rc2")));
    REQUIRE(! v.is_slic3r_supported(*Semver::parse("1.39.0")));

    v.config_version      = *Semver::parse("1.1.2-rc2");
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-alpha1")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-beta")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-beta1")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-rc")));
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.39.0-rc2")));
    REQUIRE(! v.is_slic3r_supported(*Semver::parse("1.39.0")));

    // Test the upper boundary.
    v.config_version     = *Semver::parse("1.1.2");
    v.max_slic3r_version = *Semver::parse("1.39.3-beta1");
    REQUIRE(v.is_slic3r_supported(*Semver::parse("1.38.0")));
    REQUIRE(! v.is_slic3r_supported(*Semver::parse("1.38.0-alpha")));
    REQUIRE(! v.is_slic3r_supported(*Semver::parse("1.38.0-alpha1")));
    REQUIRE(! v.is_slic3r_supported(*Semver::parse("1.37.0-alpha")));
}

TEST_CASE("Compare prerelease identifiers with numeric suffixes", "[Version]") {
    using Slic3r::Semver;

    REQUIRE(Semver("3.0.0-alpha9") < Semver("3.0.0-alpha12"));
    REQUIRE(Semver("3.0.0-alpha12") < Semver("3.0.0-alpha13"));
    REQUIRE(Semver("3.0.0-alpha12") < Semver("3.0.0-alpha100"));
    REQUIRE(Semver("3.0.0-alpha") < Semver("3.0.0-alpha1"));
    REQUIRE(Semver("3.0.0-alpha13") < Semver("3.0.0-beta1"));
    REQUIRE(Semver("3.0.0-beta2") < Semver("3.0.0-beta10"));
    REQUIRE(Semver("3.0.0-beta10") < Semver("3.0.0-rc1"));
    REQUIRE(Semver("3.0.0-rc2") < Semver("3.0.0"));
    REQUIRE(Semver("3.0.0-alpha12") == Semver("3.0.0-alpha12"));
    REQUIRE(Semver("3.0.0-alpha13") > Semver("3.0.0-alpha12"));
    REQUIRE(Semver("3.0.0-alpha.9") < Semver("3.0.0-alpha.12"));
}
