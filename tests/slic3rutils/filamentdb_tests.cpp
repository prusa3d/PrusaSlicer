// Copyright (c) 2026
// PrusaSlicer is released under the terms of the AGPLv3 or higher
//
// Tests for the FilamentDB sync helper logic. Currently focused on the
// `config_first_string` extractor — a regression target for the bug where
// commas embedded in `filament_vendor` (e.g. "Prusa Research, a.s.") were
// being interpreted as list separators and truncating the vendor before it
// reached the create payload.

#include <catch2/catch_test_macros.hpp>

#include "slic3r/Utils/FilamentDB.hpp"

#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"

using namespace Slic3r;
using filamentdb_detail::config_first_string;
using filamentdb_detail::extract_json_string;
using filamentdb_detail::bundle_to_candidates;

TEST_CASE("config_first_string: missing key returns empty", "[FilamentDB]")
{
    DynamicPrintConfig cfg;
    REQUIRE(config_first_string(cfg, "filament_vendor").empty());
    REQUIRE(config_first_string(cfg, "filament_type").empty());
}

TEST_CASE("config_first_string: coString preserves commas", "[FilamentDB]")
{
    // filament_vendor is coString — a single std::string. Embedded commas are
    // real data (e.g. legal entity suffixes) and must not be split.
    DynamicPrintConfig cfg;
    cfg.set_key_value("filament_vendor", new ConfigOptionString("Prusa Research, a.s."));

    REQUIRE(config_first_string(cfg, "filament_vendor") == "Prusa Research, a.s.");
}

TEST_CASE("config_first_string: coString preserves semicolons", "[FilamentDB]")
{
    DynamicPrintConfig cfg;
    cfg.set_key_value("filament_vendor", new ConfigOptionString("Vendor; Inc."));
    REQUIRE(config_first_string(cfg, "filament_vendor") == "Vendor; Inc.");
}

TEST_CASE("config_first_string: coString empty value yields empty string", "[FilamentDB]")
{
    DynamicPrintConfig cfg;
    cfg.set_key_value("filament_vendor", new ConfigOptionString(""));
    REQUIRE(config_first_string(cfg, "filament_vendor").empty());
}

TEST_CASE("config_first_string: coStrings returns first entry verbatim", "[FilamentDB]")
{
    // filament_type is coStrings — per-extruder. For a single filament preset
    // only [0] is set, but multi-extruder configs may stash multiple. We always
    // take values.front() and never split it.
    DynamicPrintConfig cfg;
    auto *opt = new ConfigOptionStrings();
    opt->values = { "PLA", "PETG" };
    cfg.set_key_value("filament_type", opt);

    REQUIRE(config_first_string(cfg, "filament_type") == "PLA");
}

TEST_CASE("config_first_string: coStrings empty vector yields empty string", "[FilamentDB]")
{
    DynamicPrintConfig cfg;
    cfg.set_key_value("filament_type", new ConfigOptionStrings());
    REQUIRE(config_first_string(cfg, "filament_type").empty());
}

TEST_CASE("config_first_string: coStrings preserves embedded commas in [0]", "[FilamentDB]")
{
    DynamicPrintConfig cfg;
    auto *opt = new ConfigOptionStrings();
    opt->values = { "PLA, modified" };
    cfg.set_key_value("filament_type", opt);

    REQUIRE(config_first_string(cfg, "filament_type") == "PLA, modified");
}

TEST_CASE("config_first_string: wrong type returns empty", "[FilamentDB]")
{
    // If a caller passes a key whose option is a non-string type, return empty
    // rather than serializing-and-truncating. Use a known-existing numeric key.
    DynamicPrintConfig cfg;
    cfg.set_key_value("filament_diameter", new ConfigOptionFloats({ 1.75 }));
    REQUIRE(config_first_string(cfg, "filament_diameter").empty());
}

// ---- parse_filamentdb_bundle (the INI parser used to ingest the
//      FilamentDB server's `/api/filaments/prusaslicer` response) ------------

TEST_CASE("parse_filamentdb_bundle: empty input yields no presets", "[FilamentDB]")
{
    REQUIRE(Slic3r::parse_filamentdb_bundle("").empty());
    REQUIRE(Slic3r::parse_filamentdb_bundle("\n\n#just a comment\n").empty());
}

TEST_CASE("parse_filamentdb_bundle: single section with key/value pairs", "[FilamentDB]")
{
    const std::string ini =
        "[filament:My PLA]\n"
        "filament_type = PLA\n"
        "filament_vendor = Acme\n"
        "temperature = 210\n";

    auto presets = Slic3r::parse_filamentdb_bundle(ini);
    REQUIRE(presets.size() == 1);
    CHECK(presets[0].name == "My PLA");
    CHECK(presets[0].type == "PLA");
    CHECK(presets[0].vendor == "Acme");
    REQUIRE(presets[0].config_pairs.size() == 3);
    // Order is preserved.
    CHECK(presets[0].config_pairs[0].first  == "filament_type");
    CHECK(presets[0].config_pairs[2].second == "210");
}

TEST_CASE("parse_filamentdb_bundle: multiple sections", "[FilamentDB]")
{
    const std::string ini =
        "[filament:A]\nfilament_type = PLA\n"
        "[filament:B]\nfilament_type = PETG\n";
    auto presets = Slic3r::parse_filamentdb_bundle(ini);
    REQUIRE(presets.size() == 2);
    CHECK(presets[0].name == "A");
    CHECK(presets[1].name == "B");
    CHECK(presets[1].type == "PETG");
}

TEST_CASE("parse_filamentdb_bundle: skips abstract presets", "[FilamentDB]")
{
    // PrusaSlicer convention: section names wrapped in '*…*' are inheritance
    // bases ("abstract") and should not appear as user-facing presets.
    const std::string ini =
        "[filament:*PLA*]\nfilament_type = PLA\n"  // dropped
        "[filament:Real PLA]\nfilament_vendor = Real\n";
    auto presets = Slic3r::parse_filamentdb_bundle(ini);
    REQUIRE(presets.size() == 1);
    CHECK(presets[0].name == "Real PLA");
}

TEST_CASE("parse_filamentdb_bundle: skips comments and blank lines", "[FilamentDB]")
{
    const std::string ini =
        "# Generated by FilamentDB\n"
        "\n"
        "[filament:Demo]\n"
        "# inline comment\n"
        "filament_type = PLA\n"
        "\n";
    auto presets = Slic3r::parse_filamentdb_bundle(ini);
    REQUIRE(presets.size() == 1);
    REQUIRE(presets[0].config_pairs.size() == 1);
    CHECK(presets[0].config_pairs[0].first == "filament_type");
}

TEST_CASE("parse_filamentdb_bundle: non-filament sections terminate tracking", "[FilamentDB]")
{
    // A section that doesn't start with "filament:" must reset the active
    // preset pointer so that any keys *after* it aren't mis-attributed.
    const std::string ini =
        "[filament:Real]\nfilament_type = PLA\n"
        "[print:My Print]\nlayer_height = 0.2\n"      // ignored
        "perimeter_extrusion_width = 0.45\n";          // also ignored (no active filament)
    auto presets = Slic3r::parse_filamentdb_bundle(ini);
    REQUIRE(presets.size() == 1);
    CHECK(presets[0].config_pairs.size() == 1);
    CHECK(presets[0].config_pairs[0].first == "filament_type");
}

TEST_CASE("parse_filamentdb_bundle: pre-section keys are ignored", "[FilamentDB]")
{
    // Keys that appear before any [filament:...] header have no current
    // preset to attach to and are silently dropped.
    const std::string ini =
        "filament_type = junk\n"
        "[filament:Good]\nfilament_type = PETG\n";
    auto presets = Slic3r::parse_filamentdb_bundle(ini);
    REQUIRE(presets.size() == 1);
    REQUIRE(presets[0].config_pairs.size() == 1);
    CHECK(presets[0].type == "PETG");
}

TEST_CASE("parse_filamentdb_bundle: trims whitespace around keys/values/names", "[FilamentDB]")
{
    const std::string ini =
        "[filament:   Spaced   ]\n"
        "  filament_vendor   =   Acme  \n";
    auto presets = Slic3r::parse_filamentdb_bundle(ini);
    REQUIRE(presets.size() == 1);
    CHECK(presets[0].name == "Spaced");
    REQUIRE(presets[0].config_pairs.size() == 1);
    CHECK(presets[0].config_pairs[0].first  == "filament_vendor");
    CHECK(presets[0].config_pairs[0].second == "Acme");
    CHECK(presets[0].vendor == "Acme");
}

TEST_CASE("parse_filamentdb_bundle: vendor with embedded comma survives", "[FilamentDB]")
{
    // Regression target: the parser must NOT split values on commas. This is
    // the same family of bug as the config_first_string corruption.
    const std::string ini =
        "[filament:Czech PLA]\n"
        "filament_vendor = Prusa Research, a.s.\n";
    auto presets = Slic3r::parse_filamentdb_bundle(ini);
    REQUIRE(presets.size() == 1);
    CHECK(presets[0].vendor == "Prusa Research, a.s.");
    REQUIRE(presets[0].config_pairs.size() == 1);
    CHECK(presets[0].config_pairs[0].second == "Prusa Research, a.s.");
}

TEST_CASE("parse_filamentdb_bundle: lines with no '=' are ignored within a section", "[FilamentDB]")
{
    const std::string ini =
        "[filament:Demo]\n"
        "garbage line without equals\n"
        "filament_type = PLA\n";
    auto presets = Slic3r::parse_filamentdb_bundle(ini);
    REQUIRE(presets.size() == 1);
    REQUIRE(presets[0].config_pairs.size() == 1);
    CHECK(presets[0].config_pairs[0].first == "filament_type");
}

// ---- extract_json_string (reads the #867 sync response fields:
//      matchedBy / matchedName / filamentId / error / sentName) --------------

TEST_CASE("extract_json_string: missing key returns empty", "[FilamentDB]")
{
    REQUIRE(extract_json_string("{\"a\":\"1\"}", "b").empty());
    REQUIRE(extract_json_string("", "a").empty());
}

TEST_CASE("extract_json_string: simple value", "[FilamentDB]")
{
    REQUIRE(extract_json_string("{\"matchedBy\":\"name\"}", "matchedBy") == "name");
}

TEST_CASE("extract_json_string: stops at the closing quote, ignores trailing keys", "[FilamentDB]")
{
    const std::string body = "{\"filamentId\":\"6630ab\",\"matchedName\":\"PLA Galaxy\"}";
    REQUIRE(extract_json_string(body, "filamentId") == "6630ab");
    REQUIRE(extract_json_string(body, "matchedName") == "PLA Galaxy");
}

TEST_CASE("extract_json_string: unescapes embedded quotes and backslashes", "[FilamentDB]")
{
    // value: She said "hi" \ done
    const std::string body = "{\"matchedName\":\"She said \\\"hi\\\" \\\\ done\"}";
    REQUIRE(extract_json_string(body, "matchedName") == "She said \"hi\" \\ done");
}

TEST_CASE("extract_json_string: unescapes \\n and \\t", "[FilamentDB]")
{
    REQUIRE(extract_json_string("{\"k\":\"a\\nb\\tc\"}", "k") == "a\nb\tc");
}

TEST_CASE("extract_json_string: null value yields empty", "[FilamentDB]")
{
    REQUIRE(extract_json_string("{\"matchedBy\":null}", "matchedBy").empty());
}

TEST_CASE("extract_json_string: numeric (non-string) value yields empty", "[FilamentDB]")
{
    REQUIRE(extract_json_string("{\"http\":409}", "http").empty());
}

TEST_CASE("extract_json_string: leading quote in search guards against suffix false-match", "[FilamentDB]")
{
    // Searching "name" must NOT match inside "matchedName" — the search includes
    // the opening quote ("name":), which only matches a key literally named name.
    const std::string body = "{\"matchedName\":\"X\"}";
    REQUIRE(extract_json_string(body, "name").empty());
}

TEST_CASE("extract_json_string: parses a realistic 409 name_id_mismatch body", "[FilamentDB]")
{
    const std::string body =
        "{\"error\":\"name_id_mismatch\","
        "\"message\":\"filamentdb_id resolves to ...\","
        "\"matchedBy\":\"id\",\"filamentId\":\"663012ab34cd\","
        "\"matchedName\":\"Fibreheart PPA\",\"sentName\":\"SirayaTech Fibreheart PPA\"}";
    CHECK(extract_json_string(body, "error")       == "name_id_mismatch");
    CHECK(extract_json_string(body, "filamentId")  == "663012ab34cd");
    CHECK(extract_json_string(body, "matchedName") == "Fibreheart PPA");
    CHECK(extract_json_string(body, "sentName")    == "SirayaTech Fibreheart PPA");
}

TEST_CASE("extract_json_string: tolerates whitespace around the colon", "[FilamentDB]")
{
    CHECK(extract_json_string("{\"error\": \"name_id_mismatch\"}", "error")  == "name_id_mismatch");
    CHECK(extract_json_string("{\"error\" : \"name_id_mismatch\"}", "error") == "name_id_mismatch");
    CHECK(extract_json_string("{\"error\"\n:\n  \"x\"}", "error")            == "x");
    // Pretty-printed multi-field body.
    const std::string pretty =
        "{\n  \"matchedBy\": \"id\",\n  \"filamentId\" : \"abc123\",\n  \"matchedName\": \"PLA\"\n}";
    CHECK(extract_json_string(pretty, "matchedBy")   == "id");
    CHECK(extract_json_string(pretty, "filamentId")  == "abc123");
    CHECK(extract_json_string(pretty, "matchedName") == "PLA");
}

// ---- bundle_to_candidates (turns the export INI bundle into "Link to existing"
//      choices — the #36 Phase 2 no-match flow) ------------------------------

TEST_CASE("bundle_to_candidates: empty bundle yields no candidates", "[FilamentDB]")
{
    REQUIRE(bundle_to_candidates("").empty());
    REQUIRE(bundle_to_candidates("[filament:No Id]\nfilament_type = PLA\n").empty());
}

TEST_CASE("bundle_to_candidates: extracts id/name/vendor/type", "[FilamentDB]")
{
    const std::string ini =
        "[filament:Galaxy PLA]\n"
        "filament_vendor = Acme\n"
        "filament_type = PLA\n"
        "filamentdb_id = 663012ab34cd\n";
    auto c = bundle_to_candidates(ini);
    REQUIRE(c.size() == 1);
    CHECK(c[0].id     == "663012ab34cd");
    CHECK(c[0].name   == "Galaxy PLA");
    CHECK(c[0].vendor == "Acme");
    CHECK(c[0].type   == "PLA");
}

TEST_CASE("bundle_to_candidates: sections without a filamentdb_id are skipped", "[FilamentDB]")
{
    const std::string ini =
        "[filament:Has Id]\nfilament_type = PLA\nfilamentdb_id = aaa111\n"
        "[filament:No Id]\nfilament_type = PETG\n";  // dropped — can't link without an id
    auto c = bundle_to_candidates(ini);
    REQUIRE(c.size() == 1);
    CHECK(c[0].name == "Has Id");
    CHECK(c[0].id   == "aaa111");
}

TEST_CASE("bundle_to_candidates: de-dupes by id, keeping the shortest (base) name", "[FilamentDB]")
{
    // A multi-nozzle export: three sections, one shared id. The base name is the
    // shortest; the per-nozzle suffixes only lengthen it.
    const std::string ini =
        "[filament:PA-CF 0.6 Diamondback]\nfilamentdb_id = deadbeef\n"
        "[filament:PA-CF]\nfilamentdb_id = deadbeef\n"
        "[filament:PA-CF 0.4 Brass HF]\nfilamentdb_id = deadbeef\n";
    auto c = bundle_to_candidates(ini);
    REQUIRE(c.size() == 1);
    CHECK(c[0].id   == "deadbeef");
    CHECK(c[0].name == "PA-CF");
}

TEST_CASE("bundle_to_candidates: distinct ids preserve first-seen order", "[FilamentDB]")
{
    const std::string ini =
        "[filament:Zeta]\nfilamentdb_id = id_z\n"
        "[filament:Alpha]\nfilamentdb_id = id_a\n";
    auto c = bundle_to_candidates(ini);
    REQUIRE(c.size() == 2);
    CHECK(c[0].name == "Zeta");   // bundle order preserved (server sorts by name)
    CHECK(c[1].name == "Alpha");
    CHECK(c[0].id   == "id_z");
    CHECK(c[1].id   == "id_a");
}
