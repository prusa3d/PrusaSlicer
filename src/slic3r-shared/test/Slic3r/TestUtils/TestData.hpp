#pragma once

#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <boost/filesystem.hpp>
#include <boost/optional.hpp>

#include "Slic3r/Biz/Config/3mf_legacy.hpp"
#include "Slic3r/Biz/Config/ConfigLegacy.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/OnBeds.hpp"


namespace Tests {
    inline boost::filesystem::path get_datadir() {
        namespace fs = boost::filesystem;
        return fs::absolute(fs::canonical(fs::path{TEST_DATA_DIR}));
    }

    struct Loaded3mf{
        Slic3r::Domain::Model model;
        Slic3r::Domain::ConfigPack config;
        Slic3r::Domain::WipeTowersOnBeds wipe_towers;
    };

    inline Loaded3mf load_3mf(const boost::filesystem::path &path) {
        using namespace Slic3r;
        REQUIRE(boost::filesystem::exists(path));

        Domain::ConfigPack config;
        Biz::LegacyPresetMetadata preset_metadata;
        Domain::Model model;
        Slic3r::Domain::WipeTowersOnBeds wipe_towers;
        Slic3r::Domain::CustomGCodesOnBeds custom_gcodes;

        boost::optional<Semver> version;
        Slic3r::Biz::VirtualExtrudersConfig virtual_extruders_config;
        Slic3rLegacy::load_3mf_legacy(path.string().c_str(), config, preset_metadata, &model, false, version, wipe_towers, custom_gcodes, virtual_extruders_config);

        return {model, std::move(config), std::move(wipe_towers)};
    }
}
