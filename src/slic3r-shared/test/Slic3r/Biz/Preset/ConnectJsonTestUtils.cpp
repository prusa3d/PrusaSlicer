#include "ConnectJsonTestUtils.hpp"

namespace Slic3r::Biz::Preset::ConnectJsonTestUtils {

std::vector<TestDesc> create_test_desc(bool remove_external_features)
{
    auto process_features = [remove_external_features](Domain::Preset::FeatureValueMap src)
        -> Domain::Preset::FeatureValueMap
    {
        if (remove_external_features) {
            src.erase("nozzle_hardened");
            src.erase("feeder");
        }
        return src;
    };
    return std::vector<TestDesc>{
        {{"printer_1.json"},
         {.model      = {.model = "COREONEL", .base_model = "COREONE"},
          .tool_count = 1,
          .tools      = {{process_features(
              {{"nozzle_diameter", 0.4}, {"nozzle_hardened", false}, {"nozzle_high_flow", true}}
          )}}}},
        {{"printer_2.json"},
         {.model      = {.model = "COREONE_INDX", .base_model = "COREONE_INDX"},
          .tool_count = 8,
          .tools =
              {{process_features(
                   {{"nozzle_diameter", 0.4}, {"nozzle_hardened", true}, {"nozzle_high_flow", true}}
               )},
               {process_features(
                   {{"nozzle_diameter", 0.4}, {"nozzle_hardened", true}, {"nozzle_high_flow", true}}
               )},
               {process_features(
                   {{"nozzle_diameter", 0.4}, {"nozzle_hardened", true}, {"nozzle_high_flow", true}}
               )},
               {process_features(
                   {{"nozzle_diameter", 0.4}, {"nozzle_hardened", true}, {"nozzle_high_flow", true}}
               )},
               {process_features(
                   {{"nozzle_diameter", 0.4}, {"nozzle_hardened", true}, {"nozzle_high_flow", true}}
               )},
               {process_features(
                   {{"nozzle_diameter", 0.4}, {"nozzle_hardened", true}, {"nozzle_high_flow", true}}
               )},
               {process_features(
                   {{"nozzle_diameter", 0.4}, {"nozzle_hardened", true}, {"nozzle_high_flow", true}}
               )},
               {process_features(
                   {{"nozzle_diameter", 0.4}, {"nozzle_hardened", true}, {"nozzle_high_flow", true}}
               )}}}},
        {{"printer_3.json"},
         {.model      = {.model = "XL", .base_model = "XL"},
          .tool_count = 1,
          .tools      = {{process_features(
              {{"nozzle_diameter", 0.4}, {"nozzle_hardened", false}, {"nozzle_high_flow", true}}
          )}}}},
        {{"printer_4.json"},
         {.model      = {.model = "XL", .base_model = "XL"},
          .tool_count = 5,
          .tools      = {
              {process_features(
                  {{"nozzle_diameter", 0.4}, {"nozzle_hardened", false}, {"nozzle_high_flow", true}}
              )},
              {process_features(
                  {{"nozzle_diameter", 0.4}, {"nozzle_hardened", false}, {"nozzle_high_flow", true}}
              )},
              {process_features(
                  {{"nozzle_diameter", 0.4}, {"nozzle_hardened", false}, {"nozzle_high_flow", true}}
              )},
              {process_features(
                  {{"nozzle_diameter", 0.4}, {"nozzle_hardened", false}, {"nozzle_high_flow", true}}
              )},
              {process_features(
                  {{"nozzle_diameter", 0.4}, {"nozzle_hardened", false}, {"nozzle_high_flow", true}}
              )}}}},
        {{"printer_5.json"},
         {.model      = {.model = "SL1S", .base_model = "SL1"},
          .technology = Domain::PrinterTechnology::SLA,
          .tool_count = 1,
          .tools      = {{}}}},
        {{"printer_6.json"},
         {.model      = {.model = "MK4", .base_model = "MK4"},
          .tool_count = 1,
          .tools      = {{process_features(
              {{"nozzle_diameter", 0.4},
                    {"nozzle_high_flow", false},
                    {"nozzle_hardened", false},
                    {"feeder", true}}
          )}},
         .feeders = {{{0}, {{.model="MMU3", .base_model="MMU3"}}},}}}
    };
}

} // namespace Slic3r::Biz::Preset::ConnectJsonTestUtils
