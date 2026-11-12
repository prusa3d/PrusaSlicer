#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Slic3r/Biz/Parser/PlaceholderParser.hpp"
#include "Slic3r/Domain/Config.hpp"

using namespace Catch;

using Slic3r::Biz::Parser::PlaceholderParser;
using Slic3r::Biz::Parser::IO::Config;
using Slic3r::Domain::Percentage;
using Slic3r::Domain::FloatOrPercentage;
using Slic3r::Biz::Parser::IO::Vector;
using Slic3r::Biz::Parser::IO::Scalar;
using Slic3r::Biz::Parser::IO::Value;
using Slic3r::Biz::Parser::IO::is_vector;


TEST_CASE("Placeholder parser integration test", "[PlaceholderParser][Integration]") {
}
