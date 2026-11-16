#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/UserAccount/ConnectUtils.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <variant>

using namespace Slic3r::Biz;

namespace {

PhysicalPrinter::PhysicalPrinterConfig connect_config()
{
    PhysicalPrinter::PhysicalPrinterConfig config;
    config.payload = PhysicalPrinter::ConnectUpload{};
    return config;
}

const PhysicalPrinter::ConnectUpload& upload_of(const PhysicalPrinter::PhysicalPrinterConfig& config)
{
    return std::get<PhysicalPrinter::ConnectUpload>(config.payload);
}

} // namespace

TEST_CASE("Connect PRINT message parsing", "[UserAccount][Connect]")
{
    std::string filename;
    std::string body;
    PhysicalPrinter::PhysicalPrinterConfig config = connect_config();

    SECTION("message from the Connect web view") {
        REQUIRE(UserAccount::ConnectUtils::config_from_json(
            R"({"action":"PRINT","set_ready":false,"team_id":13046,"organization_id":"046bdb58-3420-4711-ae12-a1fafeddc7c6",)"
            R"("printer_uuid":"u-1","filename":"f.bgcode","data":{"set_ready":false,"team_id":13046,)"
            R"("organization_id":"046bdb58-3420-4711-ae12-a1fafeddc7c6","printer_uuid":"u-1","filename":"f.bgcode"}})",
            config,
            filename,
            body
        ));
        CHECK(upload_of(config).organization_id == "046bdb58-3420-4711-ae12-a1fafeddc7c6");
        CHECK(upload_of(config).printer_uuid == "u-1");
        CHECK(filename == "f.bgcode");
        CHECK(nlohmann::json::parse(body)["set_ready"] == false);
    }

    SECTION("team_id is not used as the organization") {
        REQUIRE(UserAccount::ConnectUtils::config_from_json(
            R"({"team_id":10533,"printer_uuid":"u-1","filename":"f.bgcode","data":{"team_id":10533}})",
            config,
            filename,
            body
        ));
        CHECK(upload_of(config).organization_id.empty());
        CHECK(upload_of(config).team_id == "10533");
    }

    SECTION("team_id may be a string") {
        REQUIRE(UserAccount::ConnectUtils::config_from_json(
            R"({"team_id":"7","printer_uuid":"u-1","filename":"f.bgcode","data":{}})",
            config,
            filename,
            body
        ));
        CHECK(upload_of(config).team_id == "7");
    }

    SECTION("message without team_id") {
        REQUIRE(UserAccount::ConnectUtils::config_from_json(
            R"({"organization_id":"org-7","printer_uuid":"u-1","filename":"f.bgcode","data":{}})",
            config,
            filename,
            body
        ));
        CHECK(upload_of(config).team_id.empty());
    }

    SECTION("organization_id only inside data is not used") {
        REQUIRE(UserAccount::ConnectUtils::config_from_json(
            R"({"printer_uuid":"u-1","filename":"f.bgcode","data":{"organization_id":"org-7"}})",
            config,
            filename,
            body
        ));
        CHECK(upload_of(config).organization_id.empty());
    }

    SECTION("numeric organization_id is ignored") {
        REQUIRE(UserAccount::ConnectUtils::config_from_json(
            R"({"organization_id":7,"printer_uuid":"u-1","filename":"f.bgcode","data":{}})",
            config,
            filename,
            body
        ));
        CHECK(upload_of(config).organization_id.empty());
    }

    SECTION("missing printer_uuid fails") {
        CHECK_FALSE(UserAccount::ConnectUtils::config_from_json(
            R"({"organization_id":"org-7","filename":"f.bgcode","data":{}})",
            config,
            filename,
            body
        ));
    }

    SECTION("missing data fails") {
        CHECK_FALSE(UserAccount::ConnectUtils::config_from_json(
            R"({"organization_id":"org-7","printer_uuid":"u-1","filename":"f.bgcode"})",
            config,
            filename,
            body
        ));
    }

    SECTION("invalid JSON fails") {
        CHECK_FALSE(UserAccount::ConnectUtils::config_from_json("not json", config, filename, body));
    }
}
