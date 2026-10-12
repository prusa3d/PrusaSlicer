// Tests against prusalink-mock (https://github.com/hyiger/prusalink-mock), a PrusaLink server that
// answers like the Prusa Buddy firmware. Hidden: they run only under the mock's wrapper, which
// starts it and passes its URLs in the environment:
//
//     PYTHONPATH=<prusalink-mock>/src python3 -m prusalink_mock run -- slic3rutils_tests "[PrusaLinkMock]"
//
// ctest does this when CMake found the mock (see CMakeLists.txt, PRUSALINK_MOCK_DIR).

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <memory>

#include <nlohmann/json.hpp>

#include "libslic3r/Preset.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/Utils/FilamentSync.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/PrinterTools.hpp"

using namespace Slic3r;

namespace {

struct Mock
{
    std::string url;     // the printer, http://127.0.0.1:PORT
    std::string control; // the control API
    std::string key;     // API key and Digest password
};

std::string env(const char *name)
{
    const char *v = std::getenv(name);
    return v ? v : "";
}

Mock mock_or_skip()
{
    Mock m{env("PRUSALINK_MOCK_URL"), env("PRUSALINK_MOCK_CONTROL"), env("PRUSALINK_MOCK_KEY")};
    if (m.url.empty() || m.control.empty())
        SKIP("Not under prusalink-mock: python -m prusalink_mock run -- slic3rutils_tests \"[PrusaLinkMock]\"");
    return m;
}

unsigned control(const Mock &m, bool post, const std::string &path, const std::string &body = "{}", std::string *reply = nullptr)
{
    unsigned    status = 0;
    std::string got;
    auto        http = post ? Http::post(m.control + path) : Http::get(m.control + path);
    if (post)
        // Http sends a bodiless POST through its file reader; the mock needs a body anyway
        http.header("Content-Type", "application/json").set_post_body(body.empty() ? std::string("{}") : body);
    http.timeout_connect(5)
        .timeout_max(10)
        .on_complete([&](std::string b, unsigned s) { status = s; got = std::move(b); })
        .on_error([&](std::string b, std::string, unsigned s) { status = s; got = std::move(b); })
        .perform_sync();
    if (reply)
        *reply = got;
    return status;
}

// A scenario switch keeps faults and the log, and a reset reloads the last scenario: both, in this order
void prepare(const Mock &m, const std::string &scenario)
{
    REQUIRE(control(m, true, "/_mock/scenario", R"({"name":")" + scenario + R"("})") == 200);
    REQUIRE(control(m, true, "/_mock/reset") == 200);
}

void add_fault(const Mock &m, const std::string &action)
{
    // "auth":"ok": with Digest, the first request of each fetch carries no credentials
    REQUIRE(control(m, true, "/_mock/faults",
                    R"({"match":{"method":"GET","path":"/api/v1/tools","auth":"ok"},"times":1,"action":)" + action + "}") == 201);
}

// A physical printer's config, as PhysicalPrinterDialog keeps it
std::unique_ptr<DynamicPrintConfig> printer_config(const std::string &url, const std::string &key, bool digest = false,
                                                   PrintHostType type = htPrusaLink)
{
    std::unique_ptr<DynamicPrintConfig> c(DynamicPrintConfig::new_from_defaults_keys(PhysicalPrinter::printer_options()));
    c->set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(type));
    c->opt_string("print_host") = url;
    if (digest) {
        c->set_key_value("printhost_authorization_type", new ConfigOptionEnum<AuthorizationType>(atUserPassword));
        c->opt_string("printhost_user")     = "maker";
        c->opt_string("printhost_password") = key;
    } else {
        c->opt_string("printhost_apikey") = key;
    }
    return c;
}

PrinterToolsResult fetch_from(const std::string &url, const std::string &key, bool digest = false, PrinterToolsOptions opts = {})
{
    PrinterToolsResult r;
    REQUIRE(fetch_printer_tools(*printer_config(url, key, digest), r, opts));
    return r;
}

PrinterToolsResult fetch(const Mock &m, bool digest = false, PrinterToolsOptions opts = {})
{
    return fetch_from(m.url, m.key, digest, opts);
}

const PrinterTool *tool_at(const PrinterToolsResult &r, int number)
{
    for (const PrinterTool &t : r.tools.tools)
        if (t.extruder_idx == number - 1)
            return &t;
    return nullptr;
}

} // namespace

TEST_CASE("Tools of the 8-tool INDX", "[.PrusaLinkMock]")
{
    const Mock m = mock_or_skip();
    prepare(m, "indx-hy8");
    const PrinterToolsResult r = fetch(m);
    REQUIRE(r.status == PrinterToolsStatus::Ok);
    CHECK(r.http_status == 200);
    CHECK(r.tools.warnings.empty());
    REQUIRE(r.tools.tools.size() == 8);

    const PrinterTool *t5 = tool_at(r, 5);
    REQUIRE(t5);
    CHECK(t5->loaded);
    CHECK(t5->vendor == "Prusament");
    CHECK(t5->material == "PETG");
    CHECK(t5->color == "Orange");
    CHECK(t5->color_hex == "#F8651B");
    CHECK(t5->high_flow);
    CHECK_FALSE(t5->hardened);
    CHECK(tool_at(r, 7)->material == "PCTG");
    for (int empty : {4, 6, 8})
        CHECK_FALSE(tool_at(r, empty)->loaded);
}

TEST_CASE("Docks that aren't calibrated leave gaps", "[.PrusaLinkMock]")
{
    const Mock m = mock_or_skip();
    prepare(m, "indx-gaps");
    const PrinterToolsResult r = fetch(m);
    REQUIRE(r.status == PrinterToolsStatus::Ok);
    REQUIRE(r.tools.tools.size() == 4);
    CHECK(tool_at(r, 4) == nullptr);
    const PrinterTool *t5 = tool_at(r, 5);
    REQUIRE(t5);
    CHECK(t5->material == "PA");
    CHECK(t5->vendor.empty());
    CHECK(t5->hardened);
    CHECK(t5->nozzle_diameter == 0.6);

    prepare(m, "indx-no-docks");
    const PrinterToolsResult none = fetch(m);
    CHECK(none.status == PrinterToolsStatus::Ok);
    CHECK(none.tools.tools.empty());
}

TEST_CASE("Other scenarios: 0.6 nozzles, printing, the 3-tool emulator build", "[.PrusaLinkMock]")
{
    const Mock m = mock_or_skip();
    prepare(m, "indx-hy8-hf06");
    const PrinterToolsResult hf06 = fetch(m);
    REQUIRE(hf06.status == PrinterToolsStatus::Ok);
    for (const PrinterTool &t : hf06.tools.tools)
        CHECK(t.nozzle_diameter == 0.6);

    prepare(m, "indx-printing");
    CHECK(fetch(m).status == PrinterToolsStatus::Ok);

    prepare(m, "mini404-3tool");
    const PrinterToolsResult three = fetch(m);
    REQUIRE(three.status == PrinterToolsStatus::Ok);
    CHECK(three.tools.tools.size() == 3);
}

TEST_CASE("Firmware without Filament Slots is unsupported, not an error", "[.PrusaLinkMock]")
{
    const Mock m = mock_or_skip();
    for (const std::string scenario : {"indx-stock-fw", "coreone-stock"}) {
        CAPTURE(scenario);
        prepare(m, scenario);
        const PrinterToolsResult r = fetch(m);
        CHECK(r.status == PrinterToolsStatus::Unsupported);
        CHECK(r.http_status == 404);
        CHECK(r.message == "404: Not Found");
    }
}

TEST_CASE("Digest works, with the challenge first", "[.PrusaLinkMock]")
{
    const Mock m = mock_or_skip();
    prepare(m, "indx-hy8");
    const PrinterToolsResult r = fetch(m, /* digest */ true);
    REQUIRE(r.status == PrinterToolsStatus::Ok);
    CHECK(r.tools.tools.size() == 8);

    std::string log;
    REQUIRE(control(m, false, "/_mock/requests?since=0", "", &log) == 200);
    const auto           entries = nlohmann::json::parse(log).at("entries");
    std::vector<int>     statuses;
    for (const auto &e : entries)
        if (e.at("path") == "/api/v1/tools")
            statuses.push_back(e.at("status").get<int>());
    CHECK(statuses == std::vector<int>{401, 200});
}

TEST_CASE("Wrong credentials and login disabled are authentication failures", "[.PrusaLinkMock]")
{
    const Mock m = mock_or_skip();
    prepare(m, "indx-hy8");
    for (bool digest : {false, true}) {
        CAPTURE(digest);
        const PrinterToolsResult r = fetch_from(m.url, m.key + "x", digest);
        CHECK(r.status == PrinterToolsStatus::AuthFailed);
        CHECK(r.http_status == 401);
    }
    prepare(m, "login-disabled");
    CHECK(fetch(m).status == PrinterToolsStatus::AuthFailed);
}

TEST_CASE("Printer errors, broken replies and lost connections", "[.PrusaLinkMock]")
{
    const Mock m = mock_or_skip();
    prepare(m, "indx-hy8");

    add_fault(m, R"({"type":"status","status":500,"message":"boom"})");
    const PrinterToolsResult e500 = fetch(m);
    CHECK(e500.status == PrinterToolsStatus::PrinterError);
    CHECK(e500.http_status == 500);
    CHECK(e500.message == "500: Internal Server Error: boom");

    add_fault(m, R"({"type":"malformed","mode":"truncate","keep_bytes":40})");
    CHECK(fetch(m).status == PrinterToolsStatus::Malformed);

    add_fault(m, R"({"type":"malformed","mode":"html"})");
    CHECK(fetch(m).status == PrinterToolsStatus::Malformed);

    add_fault(m, R"({"type":"status","status":200,"body":"oops","content_type":"text/plain"})");
    CHECK(fetch(m).status == PrinterToolsStatus::Malformed);

    add_fault(m, R"({"type":"drop","when":"before_response"})");
    CHECK(fetch(m).status == PrinterToolsStatus::Transport);

    add_fault(m, R"({"type":"drop","when":"mid_body","after_bytes":40})");
    CHECK(fetch(m).status == PrinterToolsStatus::Transport);

    // Each fault fired once: the next fetch is fine again
    CHECK(fetch(m).status == PrinterToolsStatus::Ok);
}

TEST_CASE("A printer that hangs times out", "[.PrusaLinkMock]")
{
    const Mock m = mock_or_skip();
    prepare(m, "indx-hy8");
    add_fault(m, R"({"type":"hang","seconds":10})");
    PrinterToolsOptions opts;
    opts.timeout_max_s = 2;
    const auto               start = std::chrono::steady_clock::now();
    const PrinterToolsResult r     = fetch(m, false, opts);
    const auto               took  = std::chrono::steady_clock::now() - start;
    CHECK(r.status == PrinterToolsStatus::Transport);
    CHECK(r.http_status == 0);
    CHECK(took < std::chrono::seconds(8));
}

TEST_CASE("A connection refused is a transport failure", "[.PrusaLinkMock]")
{
    mock_or_skip();
    // The discard port: nothing listens on it
    const PrinterToolsResult r = fetch_from("http://127.0.0.1:9", "key");
    CHECK(r.status == PrinterToolsStatus::Transport);
    CHECK_FALSE(r.message.empty());
}

TEST_CASE("Odd tool entries from the printer are skipped with warnings", "[.PrusaLinkMock]")
{
    const Mock m = mock_or_skip();
    prepare(m, "indx-hy8");
    add_fault(m, R"({"type":"malformed","mode":"wrong_shape","body":{"tools":{"x":{},"2":{"loaded":"yes"},)"
                 R"("3":{"loaded":true,"material":"PLA","nozzle_diameter":0.4,"color_hex":"#abcdef"}}}})");
    const PrinterToolsResult r = fetch(m);
    REQUIRE(r.status == PrinterToolsStatus::Ok);
    REQUIRE(r.tools.tools.size() == 1);
    CHECK(r.tools.tools[0].color_hex == "#ABCDEF");
    CHECK(r.tools.warnings.size() == 2);
}

TEST_CASE("Tools that are all unreadable are a malformed reply, not no tools", "[.PrusaLinkMock]")
{
    const Mock m = mock_or_skip();
    prepare(m, "indx-hy8");
    add_fault(m, R"({"type":"malformed","mode":"wrong_shape","body":{"tools":{"x":{},"2":{"loaded":"yes"}}}})");
    const PrinterToolsResult r = fetch(m);
    CHECK(r.status == PrinterToolsStatus::Malformed);
    CHECK_FALSE(r.message.empty());
}

TEST_CASE("A config with only the host and key works", "[.PrusaLinkMock]")
{
    const Mock m = mock_or_skip();
    prepare(m, "indx-hy8");
    DynamicPrintConfig c;
    c.set_deserialize_strict({{"host_type", "prusalink"}, {"print_host", m.url}, {"printhost_apikey", m.key}});
    PrinterToolsResult r;
    REQUIRE(fetch_printer_tools(c, r));
    CHECK(r.status == PrinterToolsStatus::Ok);
}

TEST_CASE("Hosts other than PrusaLink don't report tools", "[.PrusaLinkMock]")
{
    mock_or_skip();
    for (PrintHostType type : {htOctoPrint, htPrusaConnect}) {
        PrinterToolsResult r;
        CHECK_FALSE(fetch_printer_tools(*printer_config("http://127.0.0.1:9", "key", false, type), r));
        CHECK(r.status == PrinterToolsStatus::Unsupported);
    }
}

TEST_CASE("From the printer to a plan: the 8-tool INDX against typical presets", "[.PrusaLinkMock]")
{
    using namespace filament_sync;
    const Mock m = mock_or_skip();
    prepare(m, "indx-hy8");
    const PrinterToolsResult r = fetch(m);
    REQUIRE(r.status == PrinterToolsStatus::Ok);

    const auto user = [](std::string name, std::string vendor, std::string type, std::string colour) {
        Candidate c;
        c.name = std::move(name), c.vendor = std::move(vendor), c.type = std::move(type), c.colour = std::move(colour);
        return c;
    };
    const auto system = [](std::string name, std::string vendor, std::string type) {
        Candidate c;
        c.alias = name.substr(0, name.find(" @"));
        c.name = std::move(name), c.vendor = std::move(vendor), c.type = std::move(type), c.is_system = true;
        return c;
    };
    const std::vector<Candidate> presets = {
        system("Generic PETG @COREONEINDX HF0.4", "Generic", "PETG"),
        system("Prusament PETG @COREONEINDX HF0.4", "Prusa Polymers", "PETG"),
        user("Overture PLA White", "Overture", "PLA", "#FFFEFA"),
        user("Overture PLA Matte White", "Overture", "PLA", "#E6DEDE"),
        user("Overture PETG Black", "Overture", "PETG", "#000000"),
        user("Overture ASA Gray", "Overture", "ASA", "#808080"),
        user("Prusament PETG Prusa Orange", "Prusament", "PETG", "#EB5403"),
        user("3D-Fuel Pro PCTG Black @INDX", "3D Fuel", "PCTG", "#000000"),
    };
    std::vector<ExtruderState> extruders;
    for (int i = 0; i < 8; ++i) {
        ExtruderState ex;
        ex.idx             = i;
        ex.selected        = "Generic PETG @COREONEINDX HF0.4";
        ex.nozzle_diameter = 0.4;
        ex.high_flow       = true;
        ex.candidates      = presets;
        extruders.push_back(ex);
    }
    const Plan plan = make_plan(r.tools, extruders, {}, "INDX");
    REQUIRE(plan.extruders.size() == 8);
    CHECK(plan.notes.empty());

    const std::vector<std::pair<Action, std::string>> expected = {
        {Action::Select, "Overture PLA White"},          {Action::Select, "Overture PETG Black"},
        {Action::Select, "Overture ASA Gray"},           {Action::NotLoaded, ""},
        {Action::Select, "Prusament PETG Prusa Orange"}, {Action::NotLoaded, ""},
        {Action::Select, "3D-Fuel Pro PCTG Black @INDX"}, {Action::NotLoaded, ""},
    };
    for (size_t i = 0; i < 8; ++i) {
        CAPTURE(i);
        CHECK(plan.extruders[i].action == expected[i].first);
        CHECK(plan.extruders[i].preset == expected[i].second);
        CHECK(plan.extruders[i].notes.empty());
    }
    // What each extruder shows once its color is reset to the filament's
    CHECK(plan.extruders[4].preset_colour == "#EB5403");
    CHECK(plan.extruders[3].preset_colour.empty());
}
