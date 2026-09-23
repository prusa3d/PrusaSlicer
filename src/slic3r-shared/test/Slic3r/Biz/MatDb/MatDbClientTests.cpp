#include <catch2/catch_test_macros.hpp>
#include "Slic3r/TestUtils/TestData.hpp"
#include "Slic3r/Biz/MatDb/MatDbClient.hpp"

using namespace Slic3r::Biz::MatDb;

TEST_CASE("MatDbClient Tests", "[matdb]")
{
    MatDbClient client;
    auto matdb_path = Tests::get_datadir() / "matdb/matdb-mini.zip";
    REQUIRE(client.open(matdb_path.string()));

    auto result = client.query_by_name("black", 100, 0);
    REQUIRE(result.size() > 0);
}
