#include <catch2/catch_test_macros.hpp>
#include "Slic3r/Biz/Config/HwConfigJson.hpp" // IWYU pragma: keep
#include <iostream>
#include <magic_enum/magic_enum.hpp>
#include <nlohmann/json.hpp>

using nlohmann::ordered_json;
using Slic3r::Domain::PrinterTechnology;
using Slic3r::Domain::Preset::FeatureValue;
using Slic3r::Domain::Preset::FeederType;
using Slic3r::Domain::Preset::HwFeederConfig;
using Slic3r::Domain::Preset::MaterialConfig;
using Slic3r::Domain::Preset::HwPrinterConfig;
using Slic3r::Domain::Preset::HwToolConfig;
using Slic3r::Domain::Preset::HwToolConfigs;
using Slic3r::Biz::Config::load_hw_config;

TEST_CASE("HwPrinterConfig json roundtrip", "[HwPrinterConfigRoundtrip]")
{
    const HwPrinterConfig config{
        .id = "id",
        .printer_id = "printer",
        .vendor_id = "vendor",
        .name = "name",
        .technology = PrinterTechnology::FFF,
        .model = {"model0", "base_model0"},
        .tool_count = 3,
        .features =
            {
                {"some", FeatureValue{10.0}},
                {"other", FeatureValue{false}},
            },
        .tools =
            {HwToolConfig{.id = "tool1id", .features = {{"f1", FeatureValue{"v1"}}}},
             HwToolConfig{.id = "tool2id", .features = {{"f2", FeatureValue{"v2"}}}},
             HwToolConfig{.id = "tool3id", .features = {{"f3", FeatureValue{"v3"}}}}},
        .feeders =
            {{{1, 2},
              HwFeederConfig{
                  .id = {"feeder1id"},
                  .type = FeederType::MMU,
                  .model = {"model1", "base_model1"},
                  .slot_count = 3,
                  .features =
                      {
                          {"my", FeatureValue{2.1}},
                      },
              }},
             {{3, 4},
              HwFeederConfig{
                  .id = {"feeder2id"},
                  .type = FeederType::Manual,
                  .model = {"model2", "base_model2"},
                  .slot_count = 2,
                  .features =
                      {
                          {"theirs", FeatureValue{false}},
                      },
              }}},
        .materials = {
            {{5, 6},
             MaterialConfig{
                 .id="Generic PLA",
                 .type="PLA",
                 .features = {
                     {"mat", FeatureValue{true}},
                 },
             }}
        }
    };

    ordered_json json = config;
    const auto result{load_hw_config(json)};

    INFO((result.has_value() ? "" : result.error()));
    CHECK(result.has_value());
}
