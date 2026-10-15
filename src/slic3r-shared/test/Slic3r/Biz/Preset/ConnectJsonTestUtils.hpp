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
    Domain::Preset::FeatureValueMap features;
};

using ExpectedToolResults = std::vector<ExpectedToolResult>;

struct ExpectedPrinterResult
{
    Domain::Preset::HwModel model;
    Domain::PrinterTechnology technology{Domain::PrinterTechnology::FFF};
    uint8_t tool_count{1};
    ExpectedToolResults tools;
};

using TestDesc = std::pair<TestInput, ExpectedPrinterResult>;

std::vector<TestDesc> create_test_desc(bool remove_hardend_feature=false);
} // namespace Slic3r::Biz::Preset::ConnectJsonTestUtils
