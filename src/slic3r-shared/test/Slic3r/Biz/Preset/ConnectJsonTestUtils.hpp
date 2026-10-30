#pragma once
#include <vector>

#include "Slic3r/Domain/Preset/HwConfig.hpp"

namespace Slic3r::Biz::Preset::ConnectJsonTestUtils {

struct TestInput
{
    std::string file_name;
};

struct ExpectedToolResult
{
    Domain::Preset::FeatureValueMap features;
};

struct ExpectedFeederResult
{
    Domain::Preset::HwModel model;
};

using ExpectedToolResults = std::vector<ExpectedToolResult>;
using ExpectedFeederResults = std::map<Domain::Preset::Address, ExpectedFeederResult>;

struct ExpectedPrinterResult
{
    Domain::Preset::HwModel model;
    Domain::PrinterTechnology technology{Domain::PrinterTechnology::FFF};
    uint8_t tool_count{1};
    ExpectedToolResults tools;
    ExpectedFeederResults feeders;
};

using TestDesc = std::pair<TestInput, ExpectedPrinterResult>;

std::vector<TestDesc> create_test_desc(bool remove_external_features=false);
} // namespace Slic3r::Biz::Preset::ConnectJsonTestUtils
