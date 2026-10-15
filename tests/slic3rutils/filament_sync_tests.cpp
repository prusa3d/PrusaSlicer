#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <map>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/Utils/FilamentSync.hpp"

using namespace Slic3r;
using namespace Slic3r::filament_sync;
namespace fs = boost::filesystem;

namespace {

Candidate cand(const std::string &name, const std::string &vendor, const std::string &type, const std::string &colour = "")
{
    Candidate c;
    c.name   = name;
    c.vendor = vendor;
    c.type   = type;
    c.colour = colour;
    return c;
}

Candidate sys(const std::string &name, const std::string &vendor, const std::string &type, const std::string &colour = "")
{
    Candidate c = cand(name, vendor, type, colour);
    c.is_system = true;
    if (const size_t at = name.find(" @"); at != std::string::npos)
        c.alias = name.substr(0, at);
    return c;
}

PrinterTool tool(int number, const std::string &vendor, const std::string &material, const std::string &color = "",
                 const std::string &hex = "")
{
    PrinterTool t;
    t.extruder_idx    = number - 1;
    t.loaded          = true;
    t.vendor          = vendor;
    t.material        = material;
    t.color           = color;
    t.color_hex       = hex;
    t.nozzle_diameter = 0.4;
    t.high_flow       = true;
    return t;
}

ExtruderState extruder(int idx, const std::vector<Candidate> &candidates, const std::string &selected = "")
{
    ExtruderState ex;
    ex.idx             = idx;
    ex.selected        = selected;
    ex.nozzle_diameter = 0.4;
    ex.high_flow       = true;
    ex.candidates      = candidates;
    return ex;
}

// One tool against one extruder
ExtruderPlan plan_one(const PrinterTool &t, const std::vector<Candidate> &candidates, const std::string &selected = "",
                      const std::set<std::string> &taken = {})
{
    std::vector<ExtruderState> exs;
    for (int i = 0; i <= t.extruder_idx; ++i)
        exs.push_back(extruder(i, candidates, i == t.extruder_idx ? selected : ""));
    PrinterTools tools;
    tools.tools = {t};
    const Plan plan = make_plan(tools, exs, taken, "INDX");
    REQUIRE(plan.extruders.size() == 1);
    return plan.extruders.front();
}

} // namespace

TEST_CASE("Vendor spellings fold together", "[FilamentSync]")
{
    CHECK(vendor_key("3D-Fuel") == vendor_key("3D Fuel"));
    CHECK(vendor_key("eSun") == vendor_key("Esun"));
    CHECK(vendor_key("colorFabb") == vendor_key("ColorFabb"));
    CHECK(vendor_key("Sunlu") == vendor_key("SUNLU"));
    CHECK(vendor_key("Bambu Lab") == vendor_key("BambuLab"));
    CHECK(vendor_key("Prusament") == vendor_key("Prusa Polymers"));
    CHECK(vendor_key("Overture") != vendor_key("Polymaker"));

    for (const std::string v : {"", "  ", "VENDOR1", "vendor8"})
        CHECK(is_placeholder_vendor(v));
    for (const std::string v : {"VENDOR9", "Vendor", "Overture"})
        CHECK_FALSE(is_placeholder_vendor(v));
}

TEST_CASE("Materials match by name, the flexible family and the printer's 7 characters", "[FilamentSync]")
{
    CHECK(same_material("PETG", "petg"));
    CHECK_FALSE(same_material("PETG", "PET"));
    CHECK_FALSE(same_material("PET", "PETG"));
    CHECK(same_material("FLEX", "TPU"));
    CHECK(same_material("TPU", "PEBA"));
    CHECK(same_material("PPS-CF1", "PPS-CF10"));
    CHECK(same_material("Woodfil", "Woodfill"));
    CHECK_FALSE(same_material("PLA", "PLA-GF"));
    CHECK_FALSE(same_material("", "PLA"));
}

TEST_CASE("The plain color preset wins over variants of it", "[FilamentSync]")
{
    // The user's Overture whites: none has the printer's exact #FFFFFF
    const std::vector<Candidate> c = {cand("Overture PLA Matte White", "Overture", "PLA", "#E6DEDE"),
                                      cand("Overture PLA White", "Overture", "PLA", "#FFFEFA"),
                                      cand("Overture Silk PLA White", "Overture", "PLA", "#DDD4CA"),
                                      cand("Overture PLA Black", "Overture", "PLA", "#000000")};
    const ExtruderPlan p = plan_one(tool(1, "Overture", "PLA", "White", "#FFFFFF"), c, "Generic PLA");
    CHECK(p.action == Action::Select);
    CHECK(p.preset == "Overture PLA White");
    CHECK(p.preset_colour == "#FFFEFA");
}

TEST_CASE("A new spool on the same tool selects its color's preset", "[FilamentSync]")
{
    const std::vector<Candidate> c = {cand("Overture PETG Black", "Overture", "PETG", "#000000"),
                                      cand("Overture PETG Red", "Overture", "PETG", "#ff0000")};
    CHECK(plan_one(tool(2, "Overture", "PETG", "Red", "#FF0000"), c, "Overture PETG Black").preset == "Overture PETG Red");
    const ExtruderPlan kept = plan_one(tool(2, "Overture", "PETG", "Black", "#000000"), c, "Overture PETG Black");
    CHECK(kept.action == Action::Keep);
    CHECK(kept.preset_colour == "#000000");
}

TEST_CASE("A selected variant the printer can't tell apart is kept", "[FilamentSync]")
{
    const std::vector<Candidate> c = {cand("Overture PLA Matte White", "Overture", "PLA"), cand("Overture PLA White", "Overture", "PLA")};
    CHECK(plan_one(tool(1, "Overture", "PLA", "White", "#FFFFFF"), c, "Overture PLA Matte White").action == Action::Keep);
}

TEST_CASE("Grey and gray are one color, and an exact hex counts as a match", "[FilamentSync]")
{
    const std::vector<Candidate> c = {cand("Overture PETG Grey", "Overture", "PETG", "#A0A0A0"),
                                      cand("Overture PETG Black", "Overture", "PETG", "#000000")};
    CHECK(plan_one(tool(1, "Overture", "PETG", "Gray", "#808080"), c).preset == "Overture PETG Grey");

    const std::vector<Candidate> by_hex = {cand("Overture PETG Space", "Overture", "PETG", "#808080"),
                                           cand("Overture PETG", "Overture", "PETG", "")};
    CHECK(plan_one(tool(1, "Overture", "PETG", "COLOR3", "#808080"), by_hex).preset == "Overture PETG Space");
}

TEST_CASE("Prusament matches Prusa Polymers presets, plain before other colors", "[FilamentSync]")
{
    std::vector<Candidate> c = {sys("Prusament PETG @COREONEINDX HF0.4", "Prusa Polymers", "PETG", "#F0F0F0"),
                                cand("Prusament PETG Prusa Galaxy Black", "Prusament", "PETG", "#494546")};
    const PrinterTool orange = tool(5, "Prusament", "PETG", "Orange", "#F8651B");
    CHECK(plan_one(orange, c).preset == "Prusament PETG @COREONEINDX HF0.4");

    c.push_back(cand("Prusament PETG Prusa Orange", "Prusament", "PETG", "#EB5403"));
    CHECK(plan_one(orange, c).preset == "Prusament PETG Prusa Orange");
}

TEST_CASE("A nozzle that isn't hardened avoids abrasive presets", "[FilamentSync]")
{
    Candidate cf = sys("Prusament PETG Carbon Fiber @COREONEINDX HF0.4", "Prusa Polymers", "PETG");
    cf.is_abrasive = true;
    const std::vector<Candidate> c = {cf, sys("Prusament PETG @COREONEINDX HF0.4", "Prusa Polymers", "PETG")};

    const ExtruderPlan soft = plan_one(tool(1, "Prusament", "PETG"), c, cf.name);
    CHECK(soft.action == Action::Select);
    CHECK(soft.preset == "Prusament PETG @COREONEINDX HF0.4");

    PrinterTool hard = tool(1, "Prusament", "PETG");
    hard.hardened    = true;
    CHECK(plan_one(hard, c, cf.name).action == Action::Keep);

    // The only match is abrasive: picked, with a note
    const ExtruderPlan only = plan_one(tool(1, "Prusament", "PETG"), {cf});
    CHECK(only.preset == cf.name);
    CHECK(only.notes.size() == 1);
}

TEST_CASE("Without a vendor the current preset is kept, else Generic", "[FilamentSync]")
{
    const std::vector<Candidate> c = {cand("Filatech Easy PA", "Filatech", "PA"), sys("Generic PA @SYNC", "Generic", "PA"),
                                      cand("My PLA", "Acme", "PLA")};
    PrinterTool pa = tool(5, "", "PA");
    const ExtruderPlan kept = plan_one(pa, c, "Filatech Easy PA");
    CHECK(kept.action == Action::Keep);
    CHECK_FALSE(kept.notes.empty());

    const ExtruderPlan generic = plan_one(pa, c, "My PLA");
    CHECK(generic.action == Action::Select);
    CHECK(generic.preset == "Generic PA @SYNC");

    // An unnamed user vendor is no vendor
    CHECK(plan_one(tool(1, "VENDOR2", "PA"), c, "My PLA").preset == "Generic PA @SYNC");
    // ... and is never created
    CHECK(plan_one(tool(1, "", "ASA"), c).action == Action::NoMatch);
}

TEST_CASE("Incompatible presets don't count", "[FilamentSync]")
{
    Candidate other = cand("Overture PETG Black", "Overture", "PETG");
    other.is_compatible = false;
    const ExtruderPlan p = plan_one(tool(1, "Overture", "PETG", "Black"), {other, sys("Generic PETG @SYNC", "Generic", "PETG")});
    CHECK(p.action == Action::Create);
    CHECK(p.template_preset == "Generic PETG @SYNC");
}

TEST_CASE("A system preset that isn't installed is installed", "[FilamentSync]")
{
    Candidate hidden = sys("Prusament PETG @SYNC", "Prusa Polymers", "PETG");
    hidden.is_visible = false;
    const ExtruderPlan p = plan_one(tool(1, "Prusament", "PETG"), {hidden, sys("Generic PETG @SYNC", "Generic", "PETG")});
    CHECK(p.action == Action::Install);
    CHECK(p.preset == "Prusament PETG @SYNC");
}

TEST_CASE("Missing presets are created per vendor, material and color", "[FilamentSync]")
{
    const std::vector<Candidate> c = {sys("Generic PETG @SYNC", "Generic", "PETG"), sys("Prusament PETG @SYNC", "Prusa Polymers", "PETG"),
                                      cand("My PETG", "Acme", "PETG")};
    PrinterTools tools;
    tools.tools = {tool(1, "Polymaker", "PETG", "Black", "#000000"), tool(2, "Polymaker", "PETG", "Red", "#FF0000"),
                   tool(3, "Polymaker", "PLA"), tool(4, "Polymaker", "PETG", "Black", "#000000"),
                   tool(5, "Polymaker", "PETG", "COLOR3", "#123456"), tool(6, "Polymaker", "PETG")};
    std::vector<ExtruderState> exs = {extruder(0, c, "My PETG")};
    for (int i = 1; i < 6; ++i)
        exs.push_back(extruder(i, c));
    const Plan plan = make_plan(tools, exs, {}, "INDX");
    REQUIRE(plan.extruders.size() == 6);
    CHECK(plan.extruders[0].action == Action::Create);
    CHECK(plan.extruders[0].preset == "Polymaker PETG Black");
    CHECK(plan.extruders[0].vendor == "Polymaker");
    CHECK(plan.extruders[0].preset_colour == "#000000");
    // From the first tool's extruder: its own PETG preset
    CHECK(plan.extruders[0].template_preset == "My PETG");
    // Another color is another preset, here from Generic
    CHECK(plan.extruders[1].preset == "Polymaker PETG Red");
    CHECK(plan.extruders[1].template_preset == "Generic PETG @SYNC");
    // No PLA preset to start from
    CHECK(plan.extruders[2].action == Action::NoMatch);
    CHECK(plan.extruders[2].notes.size() == 1);
    // The same color shares the first one
    CHECK(plan.extruders[3].preset == "Polymaker PETG Black");
    CHECK(plan.extruders[3].template_preset == "My PETG");
    // An unnamed user color goes by its hex; no color, no suffix
    CHECK(plan.extruders[4].preset == "Polymaker PETG #123456");
    CHECK(plan.extruders[5].preset == "Polymaker PETG");
    // ... and shows PrusaSlicer's default filament color, which it gets
    CHECK(plan.extruders[5].preset_colour == printer_tools_detail::normalize_color_hex(
                                                 print_config_def.get("filament_colour")->default_value->serialize()));
    CHECK_FALSE(plan.extruders[5].preset_colour.empty());
}

TEST_CASE("Colors without a hex are told apart by name", "[FilamentSync]")
{
    PrinterTools tools;
    tools.tools = {tool(1, "Polymaker", "PETG", "Black"), tool(2, "Polymaker", "PETG", "Red"), tool(3, "Polymaker", "PETG", "Black")};
    const std::vector<Candidate> c = {sys("Generic PETG @SYNC", "Generic", "PETG")};
    const Plan plan = make_plan(tools, {extruder(0, c), extruder(1, c), extruder(2, c)}, {}, "INDX");
    REQUIRE(plan.extruders.size() == 3);
    CHECK(plan.extruders[0].preset == "Polymaker PETG Black");
    CHECK(plan.extruders[1].preset == "Polymaker PETG Red");
    CHECK(plan.extruders[2].preset == "Polymaker PETG Black");
}

TEST_CASE("Created presets avoid taken names", "[FilamentSync]")
{
    const std::vector<Candidate> c = {sys("Generic PETG @SYNC", "Generic", "PETG")};
    const PrinterTool t = tool(1, "Sunlu", "PETG");
    CHECK(plan_one(t, c, "", {"sunlu petg"}).preset == "Sunlu PETG @INDX");
    CHECK(plan_one(t, c, "", {"sunlu petg", "sunlu petg @indx", "sunlu petg @indx 2"}).preset == "Sunlu PETG @INDX 3");

    // A name that would sort before "- default -"
    const ExtruderPlan bad = plan_one(tool(1, "&Co", "PETG"), c);
    CHECK(bad.action == Action::NoMatch);
}

TEST_CASE("Abrasive templates aren't used for a nozzle that isn't hardened", "[FilamentSync]")
{
    Candidate cf = sys("Generic PETG CF @SYNC", "Generic", "PETG");
    cf.is_abrasive = true;
    CHECK(plan_one(tool(1, "Polymaker", "PETG"), {cf}).action == Action::NoMatch);
    PrinterTool hard = tool(1, "Polymaker", "PETG");
    hard.hardened = true;
    CHECK(plan_one(hard, {cf}).action == Action::Create);
}

TEST_CASE("Empty tools, gaps and tools beyond the profile", "[FilamentSync]")
{
    const std::vector<Candidate> c = {sys("Generic PETG @SYNC", "Generic", "PETG")};
    PrinterTools tools;
    PrinterTool  empty = tool(1, "", "");
    empty.loaded       = false;
    tools.tools        = {empty, tool(3, "Generic", "PETG", "Red", "#FF0000"), tool(9, "Generic", "PETG")};
    tools.warnings     = {"tool \"x\": not a tool number, skipped"};
    const Plan plan    = make_plan(tools, {extruder(0, c), extruder(1, c), extruder(2, c, "Generic PETG @SYNC")}, {}, "INDX");
    REQUIRE(plan.extruders.size() == 2);
    CHECK(plan.extruders[0].action == Action::NotLoaded);
    CHECK(plan.extruders[0].preset_colour.empty());
    CHECK(plan.extruders[1].idx == 2);
    CHECK(plan.extruders[1].action == Action::Keep);
    // The preset has no filament color of its own here
    CHECK(plan.extruders[1].preset_colour.empty());
    // Tool 9 and the parse warning
    CHECK(plan.notes.size() == 2);
}

TEST_CASE("Nozzle differences are noted", "[FilamentSync]")
{
    PrinterTool t     = tool(1, "Generic", "PETG");
    t.nozzle_diameter = 0.6;
    t.high_flow       = false;
    const ExtruderPlan p = plan_one(t, {sys("Generic PETG @SYNC", "Generic", "PETG")});
    CHECK(p.notes.size() == 2);
}

TEST_CASE("The plan names the filament color each extruder shows", "[FilamentSync]")
{
    const std::vector<Candidate> c = {cand("Overture PETG Red", "Overture", "PETG", "#ee1111"), sys("Generic PETG @SYNC", "Generic", "PETG")};
    PrinterTools tools;
    tools.tools     = {tool(1, "Overture", "PETG", "Red", "#FF0000"), tool(2, "Polymaker", "PETG", "Blue", "#0000FF"), tool(3, "", "PETG")};
    const Plan plan = make_plan(tools, {extruder(0, c), extruder(1, c), extruder(2, c)}, {}, "INDX");
    REQUIRE(plan.extruders.size() == 3);
    // The selected preset's own color, uppercase
    CHECK(plan.extruders[0].preset_colour == "#EE1111");
    // A new preset takes the tool's color
    CHECK(plan.extruders[1].action == Action::Create);
    CHECK(plan.extruders[1].preset_colour == "#0000FF");
    // A preset without a filament color
    CHECK(plan.extruders[2].preset_colour.empty());
}

// ---------------------------------------------------------------------------------------------
// Applying a plan to a PresetBundle loaded from a temporary data folder

namespace {

class TempDataDir
{
public:
    TempDataDir() : m_old(Slic3r::data_dir())
    {
        m_dir = fs::temp_directory_path() / fs::unique_path("fsync-%%%%-%%%%");
        fs::create_directories(m_dir);
        Slic3r::set_data_dir(m_dir.string());
    }
    ~TempDataDir()
    {
        Slic3r::set_data_dir(m_old);
        boost::system::error_code ec;
        fs::remove_all(m_dir, ec);
    }
    const fs::path &dir() const { return m_dir; }

private:
    std::string m_old;
    fs::path    m_dir;
};

void write_file(const fs::path &path, const std::string &text)
{
    boost::nowide::ofstream f(path.string());
    f << text;
}

const char *VENDOR_BUNDLE = R"([vendor]
name = Sync Test
config_version = 0.0.1

[printer_model:SYNC2T]
name = Sync 2T
variants = 0.4
technology = FFF
family = SYNC

[printer:Sync 2T]
printer_technology = FFF
printer_model = SYNC2T
printer_variant = 0.4
nozzle_diameter = 0.4,0.4
extruder_colour = "";""

[filament:Generic PETG @SYNC]
filament_type = PETG
filament_vendor = Generic
filament_colour = #FF8000
compatible_printers_condition = printer_model=="SYNC2T"

[filament:Prusament PETG @SYNC]
filament_type = PETG
filament_vendor = Prusa Polymers
compatible_printers_condition = printer_model=="SYNC2T"

[filament:Generic PLA @SYNC]
filament_type = PLA
filament_vendor = Generic
compatible_printers_condition = printer_model=="SYNC2T"

[filament:Spectrum PETG Matt @SYNC]
filament_type = PETG
filament_vendor = Spectrum
filament_colour = #000000
compatible_printers_condition = printer_model=="ANOTHER"
)";

struct Fixture
{
    TempDataDir  data;
    AppConfig    app{AppConfig::EAppMode::Editor};
    PresetBundle bundle;

    // user_filaments: more user filament presets, file contents by name; first: extruder 1's preset
    explicit Fixture(const std::map<std::string, std::string> &user_filaments = {}, const std::string &first = "Generic PETG @SYNC")
    {
        bundle.setup_directories();
        write_file(data.dir() / "vendor" / "SyncTest.ini", VENDOR_BUNDLE);
        write_file(data.dir() / "filament" / "My PLA.ini", "filament_type = PLA\nfilament_vendor = Acme\nfilament_colour = #ffffff\n");
        for (const auto &[name, text] : user_filaments)
            write_file(data.dir() / "filament" / (name + ".ini"), text);
        app.set("filamentdb_url", "");
        app.set_variant("SyncTest", "SYNC2T", "0.4", true);
        app.set(AppConfig::SECTION_FILAMENTS, "Generic PETG @SYNC", "1");
        app.set(AppConfig::SECTION_FILAMENTS, "Generic PLA @SYNC", "1");
        app.set("presets", "printer", "Sync 2T");
        app.set("presets", "filament", first);
        app.set("presets", "filament_1", "My PLA");
        bundle.load_presets(app, ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
    }

    Plan plan_for(const PrinterTools &tools) { return make_plan(tools, collect_extruder_states(bundle), taken_filament_names(bundle.filaments), printer_name_tag(bundle)); }
};

} // namespace

TEST_CASE("The fixture loads the printer, its filaments and the selections", "[FilamentSync]")
{
    Fixture f;
    REQUIRE(f.bundle.printers.get_selected_preset_name() == "Sync 2T");
    REQUIRE(f.bundle.extruders_filaments.size() == 2);
    CHECK(f.bundle.extruders_filaments[0].get_selected_preset_name() == "Generic PETG @SYNC");
    CHECK(f.bundle.extruders_filaments[1].get_selected_preset_name() == "My PLA");
    const Preset *hidden = f.bundle.filaments.find_preset("Prusament PETG @SYNC", false, false);
    REQUIRE(hidden != nullptr);
    CHECK_FALSE(hidden->is_visible);

    const std::vector<ExtruderState> exs = f.bundle.extruders_filaments.empty() ? std::vector<ExtruderState>{} : collect_extruder_states(f.bundle);
    REQUIRE(exs.size() == 2);
    CHECK(exs[1].selected == "My PLA");
    CHECK(exs[0].nozzle_diameter == 0.4);
    CHECK(printer_name_tag(f.bundle) == "SYNC2T");
    const std::set<std::string> taken = taken_filament_names(f.bundle.filaments);
    CHECK(taken.count("my pla"));
    CHECK(taken.count("generic petg"));   // an alias
}

TEST_CASE("Applying creates, installs and selects, and survives a restart", "[FilamentSync]")
{
    Fixture      f;
    PrinterTools tools;
    tools.tools = {tool(1, "Prusament", "PETG", "Orange", "#F8651B"), tool(2, "Polymaker", "PLA", "Red", "#FF0000")};
    const Plan plan = f.plan_for(tools);
    REQUIRE(plan.extruders.size() == 2);
    REQUIRE(plan.extruders[0].action == Action::Install);
    REQUIRE(plan.extruders[1].action == Action::Create);
    CHECK(plan.extruders[1].preset == "Polymaker PLA Red");

    const ApplyResult r = apply_presets(f.bundle, &f.app, plan, {true, true});
    CHECK(r.errors.empty());
    CHECK(r.installed == std::vector<std::string>{"Prusament PETG @SYNC"});
    CHECK(r.created == std::vector<std::string>{"Polymaker PLA Red"});
    CHECK(r.selected == std::vector<int>{0, 1});
    CHECK(f.bundle.extruders_filaments[0].get_selected_preset_name() == "Prusament PETG @SYNC");
    CHECK(f.bundle.extruders_filaments[1].get_selected_preset_name() == "Polymaker PLA Red");
    CHECK(f.app.get(AppConfig::SECTION_FILAMENTS, "Prusament PETG @SYNC") == "1");

    const Preset *created = f.bundle.filaments.find_preset("Polymaker PLA Red", false, false);
    REQUIRE(created != nullptr);
    CHECK_FALSE(created->is_system);
    CHECK(created->vendor == nullptr);
    CHECK(created->config.opt_string("filament_vendor") == "Polymaker");
    CHECK(created->config.opt_string("filament_colour", 0u) == "#FF0000");
    CHECK(created->config.opt_string("filament_type", 0u) == "PLA");
    CHECK(created->config.opt_string("filamentdb_id").empty());
    CHECK(fs::exists(created->file));

    CHECK(plan.extruders[1].preset_colour == "#FF0000");

    f.bundle.export_selections(f.app);
    PresetBundle again;
    again.load_presets(f.app, ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
    REQUIRE(again.extruders_filaments.size() == 2);
    CHECK(again.extruders_filaments[0].get_selected_preset_name() == "Prusament PETG @SYNC");
    CHECK(again.extruders_filaments[1].get_selected_preset_name() == "Polymaker PLA Red");
    const Preset *reloaded = again.filaments.find_preset("Polymaker PLA Red", false, false);
    REQUIRE(reloaded != nullptr);
    CHECK(reloaded->config.opt_string("filament_vendor") == "Polymaker");
}

TEST_CASE("Rows that aren't accepted change nothing", "[FilamentSync]")
{
    Fixture      f;
    PrinterTools tools;
    tools.tools = {tool(1, "Prusament", "PETG"), tool(2, "Polymaker", "PLA")};
    const Plan        plan = f.plan_for(tools);
    const ApplyResult r    = apply_presets(f.bundle, &f.app, plan, {false, false});
    CHECK(r.errors.empty());
    CHECK(r.created.empty());
    CHECK(r.selected.empty());
    CHECK(f.bundle.filaments.find_preset("Polymaker PLA", false, false) == nullptr);
    CHECK(f.bundle.extruders_filaments[1].get_selected_preset_name() == "My PLA");
    CHECK(f.app.get(AppConfig::SECTION_FILAMENTS, "Prusament PETG @SYNC").empty());
}

TEST_CASE("Unsaved filament edits survive creating a preset", "[FilamentSync]")
{
    Fixture f;
    f.bundle.filaments.get_edited_preset().config.set_key_value("filament_notes", new ConfigOptionStrings({"keep me"}));
    f.bundle.filaments.update_dirty();
    REQUIRE(f.bundle.filaments.current_is_dirty());

    PrinterTools tools;
    tools.tools       = {tool(2, "Polymaker", "PLA")};
    const ApplyResult r = apply_presets(f.bundle, &f.app, f.plan_for(tools), {true});
    CHECK(r.errors.empty());
    CHECK(r.created.size() == 1);
    CHECK(f.bundle.filaments.current_is_dirty());
    CHECK(f.bundle.filaments.get_edited_preset().config.opt_string("filament_notes", 0u) == "keep me");
}

TEST_CASE("A name taken since the plan isn't overwritten", "[FilamentSync]")
{
    Fixture      f;
    PrinterTools tools;
    tools.tools     = {tool(2, "Polymaker", "PLA")};
    const Plan plan = f.plan_for(tools);
    REQUIRE(plan.extruders[0].action == Action::Create);
    // Another program wrote the file meanwhile
    write_file(f.data.dir() / "filament" / "polymaker pla.ini", "filament_type = PLA\n");

    const ApplyResult r = apply_presets(f.bundle, &f.app, plan, {true});
    CHECK(r.created.empty());
    CHECK(r.errors.size() == 1);
    CHECK(f.bundle.extruders_filaments[1].get_selected_preset_name() == "My PLA");
}

TEST_CASE("A tool without a color keeps a preset that names one", "[FilamentSync]")
{
    const std::vector<Candidate> c = {cand("Overture PETG Black", "Overture", "PETG", "#000000"),
                                      cand("Overture PETG", "Overture", "PETG"), sys("Generic PETG @SYNC", "Generic", "PETG")};
    // No vendor, no color: loaded by G-code
    CHECK(plan_one(tool(1, "", "PETG"), c, "Overture PETG Black").action == Action::Keep);
    // A vendor and no color
    CHECK(plan_one(tool(1, "Overture", "PETG"), c, "Overture PETG Black").action == Action::Keep);
    // A new pick still prefers the preset without a color
    CHECK(plan_one(tool(1, "Overture", "PETG"), c, "Generic PETG @SYNC").preset == "Overture PETG");
}

TEST_CASE("A user color is named by its hex in notes", "[FilamentSync]")
{
    // Without a vendor nothing is created, so the closest color is picked
    const ExtruderPlan p = plan_one(tool(1, "", "PETG", "COLOR3", "#123456"), {cand("Overture PETG Black", "Overture", "PETG", "#000000")});
    CHECK(p.action == Action::Select);
    CHECK(std::any_of(p.notes.begin(), p.notes.end(), [](const std::string &n) { return n.find("#123456") != std::string::npos; }));
}

TEST_CASE("A filament whose presets are all another color gets its own", "[FilamentSync]")
{
    // Made by an earlier sync
    const std::vector<Candidate> c = {cand("Polymaker PETG Black", "Polymaker", "PETG", "#000000"), sys("Generic PETG @SYNC", "Generic", "PETG")};
    const ExtruderPlan red = plan_one(tool(1, "Polymaker", "PETG", "Red", "#FF0000"), c, "Polymaker PETG Black");
    CHECK(red.action == Action::Create);
    CHECK(red.preset == "Polymaker PETG Red");
    // The extruder's own preset, with the settings the user tuned
    CHECK(red.template_preset == "Polymaker PETG Black");
    CHECK(red.preset_colour == "#FF0000");
    CHECK(red.notes.empty());

    CHECK(plan_one(tool(1, "Polymaker", "PETG", "Black", "#000000"), c, "Polymaker PETG Black").action == Action::Keep);
    // A preset without a color in its name still fits when its color is still its parent's ...
    std::vector<Candidate> plain = c;
    Candidate polymaker     = cand("Polymaker PETG", "Polymaker", "PETG", "#FF8000");
    polymaker.parent        = "Generic PETG";
    polymaker.parent_colour = "#ff8000";
    plain.push_back(polymaker);
    CHECK(plan_one(tool(1, "Polymaker", "PETG", "Red", "#FF0000"), plain, "Polymaker PETG Black").preset == "Polymaker PETG");
    // ... but not when its own color is another one
    plain.back().colour = "#000000";
    CHECK(plan_one(tool(1, "Polymaker", "PETG", "Red", "#FF0000"), plain, "Polymaker PETG Black").preset == "Polymaker PETG Red");
}

TEST_CASE("A user preset's own color counts, whatever its name says", "[FilamentSync]")
{
    // Filament DB presets, named for the vendor's product lines
    const std::vector<Candidate> c = {cand("PolyLite PLA Teal", "Polymaker", "PLA", "#48B9C2"),
                                      cand("PolyTerra PLA Red", "Polymaker", "PLA", "#C0392B"),
                                      sys("Generic PLA @SYNC", "Generic", "PLA")};
    const ExtruderPlan white = plan_one(tool(4, "Polymaker", "PLA", "White", "#FFFFFF"), c, "PolyLite PLA Teal");
    CHECK(white.action == Action::Create);
    CHECK(white.preset == "Polymaker PLA White");
    CHECK(white.template_preset == "PolyLite PLA Teal");
    // A teal spool keeps it
    CHECK(plan_one(tool(4, "Polymaker", "PLA", "Teal", "#48B9C2"), c, "PolyLite PLA Teal").action == Action::Keep);

    // A shade the printer's color names lump together is the same color
    std::vector<Candidate> cotton = c;
    cotton.push_back(cand("PolyLite PLA Cotton", "Polymaker", "PLA", "#E6DEDE"));
    CHECK(plan_one(tool(4, "Polymaker", "PLA", "White", "#FFFFFF"), cotton, "PolyLite PLA Teal").preset == "PolyLite PLA Cotton");

    // A system preset's color is a placeholder
    std::vector<Candidate> system = c;
    system.push_back(sys("Polymaker PolyLite PLA @SYNC", "Polymaker", "PLA", "#48B9C2"));
    CHECK(plan_one(tool(4, "Polymaker", "PLA", "White", "#FFFFFF"), system, "PolyLite PLA Teal").preset == "Polymaker PolyLite PLA @SYNC");

    // Without a vendor, the current preset stays whatever its color
    CHECK(plan_one(tool(4, "", "PLA", "White", "#FFFFFF"), c, "PolyLite PLA Teal").action == Action::Keep);
}

TEST_CASE("A system preset's variant name isn't a color", "[FilamentSync]")
{
    // Filament colors as in PrusaResearch.ini, none of them one of the printer's colors
    Candidate cf = sys("Prusament PETG Carbon Fiber @SYNC", "Prusa Polymers", "PETG", "#BBBBBB");
    cf.is_abrasive = true;
    PrinterTool black = tool(1, "Prusament", "PETG", "Black", "#000000");
    black.hardened    = true;
    CHECK(plan_one(black, {cf, sys("Prusament PETG @SYNC", "Prusa Polymers", "PETG", "#F0F0F0")}, cf.name).action == Action::Keep);

    const std::vector<Candidate> pla = {sys("Prusament PLA Blend @SYNC", "Prusa Polymers", "PLA", "#FF8000"),
                                        sys("Prusament PLA @SYNC", "Prusa Polymers", "PLA", "#F0F0F0")};
    CHECK(plan_one(tool(1, "Prusament", "PLA", "Gray", "#808080"), pla, "Prusament PLA Blend @SYNC").action == Action::Keep);
    // ... and neither is it in the user's tuned copy saved from the preset
    std::vector<Candidate> tuned = pla;
    Candidate copy     = cand("Prusament PLA Blend Tuned", "Prusa Polymers", "PLA", "#FF8000");
    copy.parent        = "Prusament PLA Blend";
    copy.parent_colour = "#FF8000";
    tuned.push_back(copy);
    CHECK(plan_one(tool(1, "Prusament", "PLA", "Gray", "#808080"), tuned, copy.name).action == Action::Keep);
    // A preset of the user's own with a color the printer named is still another color: not kept
    Candidate clear = cand("Prusament PLA Clear", "Prusament", "PLA", "#F0F0F0");
    clear.parent    = "Prusament PLA";
    tuned.push_back(clear);
    CHECK(plan_one(tool(1, "Prusament", "PLA", "Red", "#FF0000"), tuned, clear.name).preset == "Prusament PLA @SYNC");

    // A preset the sync made for a user color that starts like a variant's name ("Matt Olive",
    // the variant for another printer) isn't a copy of it: it doesn't inherit the variant
    Candidate matt = sys("Spectrum PETG Matt @SYNC", "Spectrum", "PETG", "#000000");
    matt.is_compatible = false;
    Candidate olive    = cand("Spectrum PETG Matt Olive", "Spectrum", "PETG", "#556B2F");
    olive.parent       = "Generic PETG";
    const ExtruderPlan red = plan_one(tool(1, "Spectrum", "PETG", "Red", "#FF0000"), {matt, olive, sys("Generic PETG @SYNC", "Generic", "PETG")}, olive.name);
    CHECK(red.action == Action::Create);
    CHECK(red.preset == "Spectrum PETG Red");

    // The vendor's only flexible preset is selected, not replaced by a new one
    const ExtruderPlan flex = plan_one(tool(1, "Prusament", "FLEX", "Black", "#000000"),
                                       {sys("Prusament TPU 95A @SYNC", "Prusa Polymers", "FLEX", "#F9F3E5"), sys("Generic FLEX @SYNC", "Generic", "FLEX")});
    CHECK(flex.action == Action::Select);
    CHECK(flex.preset == "Prusament TPU 95A @SYNC");
}

TEST_CASE("A system preset in the tool's color beats an installed one in another", "[FilamentSync]")
{
    Candidate orange = sys("Prusament PETG Prusa Orange @SYNC", "Prusa Polymers", "PETG", "#EB5403");
    orange.is_visible = false;
    Candidate black   = sys("Prusament PETG Jet Black @SYNC", "Prusa Polymers", "PETG", "#000000");
    black.is_visible  = false;
    const Candidate galaxy = cand("Prusament PETG Prusa Galaxy Black", "Prusament", "PETG", "#494546");
    const PrinterTool tool_orange = tool(1, "Prusament", "PETG", "Orange", "#F8651B");

    const ExtruderPlan p = plan_one(tool_orange, {galaxy, orange, black});
    CHECK(p.action == Action::Install);
    CHECK(p.preset == orange.name);
    // Another color either way: a preset of its own
    const ExtruderPlan q = plan_one(tool_orange, {galaxy, black});
    CHECK(q.action == Action::Create);
    CHECK(q.preset == "Prusament PETG Orange");
}

TEST_CASE("Another color is picked when no preset of the tool's own can be made", "[FilamentSync]")
{
    Candidate cf = cand("Polymaker PETG CF Black", "Polymaker", "PETG", "#000000");
    cf.is_abrasive = true;
    const ExtruderPlan p = plan_one(tool(1, "Polymaker", "PETG", "Red", "#FF0000"), {cf});
    CHECK(p.action == Action::Select);
    CHECK(p.preset == cf.name);
    // Why nothing was made, abrasive, and the closest color
    CHECK(p.notes.size() == 3);
}

TEST_CASE("A preset named by a hex is that color", "[FilamentSync]")
{
    const std::vector<Candidate> c = {cand("Polymaker PETG #123456", "Polymaker", "PETG", "#123456"), sys("Generic PETG @SYNC", "Generic", "PETG")};
    CHECK(plan_one(tool(1, "Polymaker", "PETG", "COLOR3", "#123456"), c).preset == "Polymaker PETG #123456");
    const ExtruderPlan other = plan_one(tool(1, "Polymaker", "PETG", "COLOR4", "#654321"), c);
    CHECK(other.action == Action::Create);
    CHECK(other.preset == "Polymaker PETG #654321");
    CHECK(plan_one(tool(1, "Polymaker", "PETG", "Red", "#FF0000"), c).preset == "Polymaker PETG Red");

    // The name says the color even when the preset's filament color was edited since, so it beats
    // a preset without a color in its name
    const std::vector<Candidate> edited = {cand("Polymaker PETG #123456", "Polymaker", "PETG", "#111111"),
                                           cand("Polymaker PETG", "Polymaker", "PETG", "#111111")};
    CHECK(plan_one(tool(1, "Polymaker", "PETG", "COLOR3", "#123456"), edited).preset == "Polymaker PETG #123456");
    // ... and doesn't take the color it was edited to as well
    const ExtruderPlan to = plan_one(tool(1, "Polymaker", "PETG", "COLOR4", "#654321"), {cand("Polymaker PETG #123456", "Polymaker", "PETG", "#654321")});
    CHECK(to.action == Action::Create);
    CHECK(to.preset == "Polymaker PETG #654321");
}

TEST_CASE("A color the printer's list doesn't have is still another color", "[FilamentSync]")
{
    // An earlier sync made this for a user color the printer has named "Clear"
    const std::vector<Candidate> c = {cand("Polymaker PETG Clear", "Polymaker", "PETG", "#F0F0F0"),
                                      cand("Polymaker PETG", "Polymaker", "PETG"), sys("Generic PETG @SYNC", "Generic", "PETG")};
    CHECK(plan_one(tool(1, "Polymaker", "PETG", "Clear", "#F0F0F0"), c, "Polymaker PETG Clear").action == Action::Keep);
    const ExtruderPlan red = plan_one(tool(1, "Polymaker", "PETG", "Red", "#FF0000"), c, "Polymaker PETG Clear");
    CHECK(red.action == Action::Select);
    // Not the clear one: the preset without a color in its name
    CHECK(red.preset == "Polymaker PETG");

    // The user's own name for a color
    const std::vector<Candidate> space = {cand("Overture PETG Space", "Overture", "PETG", "#808080"), sys("Generic PETG @SYNC", "Generic", "PETG")};
    const ExtruderPlan p = plan_one(tool(1, "Overture", "PETG", "Red", "#FF0000"), space, "Overture PETG Space");
    CHECK(p.action == Action::Create);
    CHECK(p.preset == "Overture PETG Red");
    CHECK(p.template_preset == "Overture PETG Space");
    // A preset without a filament color can't be told apart
    CHECK(plan_one(tool(1, "Overture", "PETG", "Red", "#FF0000"), {cand("Overture PETG Space", "Overture", "PETG")}).action == Action::Select);

    // The vendor and material as matching spells them
    const ExtruderPlan bambu = plan_one(tool(1, "Bambu Lab", "PETG", "Red", "#FF0000"), {cand("BambuLab PETG Clear", "BambuLab", "PETG", "#F0F0F0")});
    CHECK(bambu.action == Action::Create);
    CHECK(bambu.preset == "Bambu Lab PETG Red");
    const ExtruderPlan tpu = plan_one(tool(1, "Overture", "TPU", "Red", "#FF0000"), {cand("Overture TPE Clear", "Overture", "TPE", "#F0F0F0")});
    CHECK(tpu.action == Action::Create);
    CHECK(tpu.preset == "Overture TPU Red");
}

TEST_CASE("Tools share a new preset only when its template suits each extruder", "[FilamentSync]")
{
    // Extruder 1's template isn't compatible with extruder 2 (a different nozzle in the profile)
    Candidate hf = sys("Generic PETG @SYNC HF", "Generic", "PETG");
    Candidate sf = sys("Generic PETG @SYNC", "Generic", "PETG");
    Candidate hf_off = hf, sf_off = sf;
    hf_off.is_compatible = false;
    sf_off.is_compatible = false;

    PrinterTools tools;
    tools.tools     = {tool(1, "Polymaker", "PETG"), tool(2, "Polymaker", "PETG"), tool(3, "Polymaker", "PETG")};
    const Plan plan = make_plan(tools, {extruder(0, {hf, sf_off}), extruder(1, {hf_off, sf}), extruder(2, {hf, sf_off})}, {}, "INDX");
    REQUIRE(plan.extruders.size() == 3);
    for (const ExtruderPlan &row : plan.extruders)
        REQUIRE(row.action == Action::Create);
    CHECK(plan.extruders[0].template_preset == hf.name);
    CHECK(plan.extruders[1].template_preset == sf.name);
    CHECK(plan.extruders[0].preset != plan.extruders[1].preset);
    // The third tool's extruder can use the first one's template: shared
    CHECK(plan.extruders[2].preset == plan.extruders[0].preset);

    // An abrasive template made for a hardened nozzle isn't shared with one that isn't
    Candidate cf = sys("Generic PETG CF @SYNC", "Generic", "PETG");
    cf.is_abrasive   = true;
    PrinterTool hard = tool(1, "Polymaker", "PETG");
    hard.hardened    = true;
    PrinterTools mixed;
    mixed.tools          = {hard, tool(2, "Polymaker", "PETG")};
    const Plan plan2     = make_plan(mixed, {extruder(0, {cf}, cf.name), extruder(1, {cf, sf})}, {}, "INDX");
    REQUIRE(plan2.extruders.size() == 2);
    CHECK(plan2.extruders[0].template_preset == cf.name);
    CHECK(plan2.extruders[1].template_preset == sf.name);
    CHECK(plan2.extruders[0].preset != plan2.extruders[1].preset);
}

TEST_CASE("A new color isn't started from a copy of a variant its name continues", "[FilamentSync]")
{
    // The user's preset saved from a Spectrum variant whose system presets are for other printers
    Candidate matt     = sys("Spectrum PETG Matt @SYNC", "Spectrum", "PETG", "#000000");
    matt.is_compatible = false;
    Candidate black    = cand("Spectrum PETG Matt Black", "Spectrum", "PETG", "#000000");
    black.parent       = "Spectrum PETG Matt";
    const Candidate        generic = sys("Generic PETG @SYNC", "Generic", "PETG");
    std::vector<Candidate> c       = {matt, black, generic};

    // A user color "Matt Olive": started from the selected preset, the new one would inherit the
    // variant and pass for a copy of it
    const ExtruderPlan olive = plan_one(tool(1, "Spectrum", "PETG", "Matt Olive", "#556B2F"), c, black.name);
    REQUIRE(olive.action == Action::Create);
    CHECK(olive.preset == "Spectrum PETG Matt Olive");
    CHECK(olive.template_preset == generic.name);

    // Made from Generic, it's the olive color: the next sync with a red spool makes a red one,
    // started from the olive one
    Candidate made = cand(olive.preset, "Spectrum", "PETG", "#556B2F");
    made.parent    = "Generic PETG";
    c.push_back(made);
    const ExtruderPlan red = plan_one(tool(1, "Spectrum", "PETG", "Red", "#FF0000"), c, made.name);
    CHECK(red.action == Action::Create);
    CHECK(red.preset == "Spectrum PETG Red");
    CHECK(red.template_preset == made.name);

    // A color that doesn't start like the variant's name still starts from the selected preset
    const ExtruderPlan plain = plan_one(tool(1, "Spectrum", "PETG", "Olive", "#556B2F"), {matt, black, generic}, black.name);
    CHECK(plain.preset == "Spectrum PETG Olive");
    CHECK(plain.template_preset == black.name);
    // ... and a copy of the variant is still a template when nothing else suits the extruder
    CHECK(plan_one(tool(1, "Spectrum", "PETG", "Matt Olive", "#556B2F"), {matt, black}, black.name).template_preset == black.name);
}

TEST_CASE("The preview offers a new preset and the extruder's other presets", "[FilamentSync]")
{
    Candidate hidden  = sys("Overture PLA @SYNC", "Overture", "PLA");
    hidden.is_visible = false;
    const std::vector<Candidate> c = {cand("Overture PLA Black", "Overture", "PLA", "#000000"),
                                      cand("Overture PLA White", "Overture", "PLA", "#FFFFFF"),
                                      sys("Generic PLA @SYNC", "Generic", "PLA", "#FF8000"), hidden,
                                      cand("Overture PETG Black", "Overture", "PETG", "#000000")};
    const ExtruderPlan black = plan_one(tool(1, "Overture", "PLA", "Black", "#000000"), c, "Overture PLA Black",
                                        {"overture pla black", "overture pla white"});
    REQUIRE(black.action == Action::Keep);
    REQUIRE(black.alternatives.size() == 4);
    // A new preset of the tool's color, from the extruder's own
    CHECK(black.alternatives[0].action == Action::Create);
    CHECK(black.alternatives[0].preset == "Overture PLA Black @INDX");
    CHECK(black.alternatives[0].template_preset == "Overture PLA Black");
    CHECK(black.alternatives[0].vendor == "Overture");
    CHECK(black.alternatives[0].preset_colour == "#000000");
    // The vendor's other presets of the material, then its system ones to install, then others'
    CHECK(black.alternatives[1].action == Action::Select);
    CHECK(black.alternatives[1].preset == "Overture PLA White");
    CHECK(black.alternatives[1].preset_colour == "#FFFFFF");
    CHECK(black.alternatives[2].action == Action::Install);
    CHECK(black.alternatives[2].preset == "Overture PLA @SYNC");
    CHECK(black.alternatives[3].action == Action::Select);
    CHECK(black.alternatives[3].preset == "Generic PLA @SYNC");

    // A row that creates offers keeping the extruder's preset among the others, closest color first
    const std::vector<Candidate> poly = {cand("PolyLite PLA Teal", "Polymaker", "PLA", "#48B9C2"),
                                         cand("PolyTerra PLA Red", "Polymaker", "PLA", "#C0392B"),
                                         sys("Generic PLA @SYNC", "Generic", "PLA")};
    const ExtruderPlan white = plan_one(tool(4, "Polymaker", "PLA", "White", "#FFFFFF"), poly, "PolyLite PLA Teal");
    REQUIRE(white.action == Action::Create);
    REQUIRE(white.alternatives.size() == 3);
    CHECK(white.alternatives[0].action == Action::Keep);
    CHECK(white.alternatives[0].preset == "PolyLite PLA Teal");
    CHECK(white.alternatives[1].preset == "PolyTerra PLA Red");
    CHECK(white.alternatives[2].preset == "Generic PLA @SYNC");
    const ExtruderPlan kept = with_alternative(white, 0);
    CHECK(kept.action == Action::Keep);
    CHECK(kept.preset == "PolyLite PLA Teal");
    CHECK(kept.preset_colour == "#48B9C2");
    CHECK(kept.template_preset.empty());
    CHECK(with_alternative(white, 3).action == Action::Create);

    // Keeping a preset of another material comes last
    const ExtruderPlan petg = plan_one(tool(1, "Overture", "PETG", "Black", "#000000"), c, "Overture PLA White", {"overture petg black"});
    REQUIRE(petg.action == Action::Select);
    REQUIRE(!petg.alternatives.empty());
    CHECK(petg.alternatives.front().action == Action::Create);
    CHECK(petg.alternatives.back().action == Action::Keep);
    CHECK(petg.alternatives.back().preset == "Overture PLA White");
    CHECK(petg.alternatives.back().preset_colour == "#FFFFFF");

    // Without a vendor nothing is made or installed, and every preset of the material is offered
    const ExtruderPlan unnamed = plan_one(tool(1, "", "PLA", "Black", "#000000"), c, "Overture PLA Black");
    REQUIRE(unnamed.action == Action::Keep);
    REQUIRE(unnamed.alternatives.size() == 2);
    for (const Alternative &a : unnamed.alternatives)
        CHECK(a.action == Action::Select);
}

TEST_CASE("Each alternative comes with its own notes", "[FilamentSync]")
{
    // A 0.6 mm nozzle that isn't hardened, on a 0.4 mm profile
    PrinterTool t     = tool(1, "Overture", "PETG", "Black", "#000000");
    t.nozzle_diameter = 0.6;
    Candidate cf      = cand("Overture PETG CF Black", "Overture", "PETG", "#000000");
    cf.is_abrasive    = true;
    const Candidate plain   = cand("Overture PETG Black", "Overture", "PETG", "#000000");
    const Candidate generic = sys("Generic PETG @SYNC", "Generic", "PETG");
    const auto      has     = [](const std::vector<std::string> &notes, const std::string &part) {
        return std::any_of(notes.begin(), notes.end(), [&](const std::string &n) { return n.find(part) != std::string::npos; });
    };
    const auto find = [](const ExtruderPlan &row, const std::string &preset) {
        const auto it = std::find_if(row.alternatives.begin(), row.alternatives.end(), [&](const Alternative &a) { return a.preset == preset; });
        REQUIRE(it != row.alternatives.end());
        return size_t(it - row.alternatives.begin());
    };

    // Picking the abrasive preset instead warns about it
    const ExtruderPlan safe = plan_one(t, {plain, cf, generic}, plain.name, {"overture petg black"});
    REQUIRE(safe.action == Action::Keep);
    CHECK_FALSE(has(safe.notes, "abrasive"));
    const ExtruderPlan risky = with_alternative(safe, find(safe, cf.name));
    CHECK(risky.preset == cf.name);
    CHECK(has(risky.notes, "Overture PETG CF Black is abrasive"));
    CHECK(has(risky.notes, "0.60 mm nozzle"));

    // ... and picking a safe one instead of an abrasive pick drops the warning, not the nozzle's
    const ExtruderPlan pick = plan_one(t, {cf, generic}, cf.name, {"overture petg black"});
    REQUIRE(pick.preset == cf.name);
    CHECK(has(pick.notes, "abrasive"));
    for (const std::string preset : {std::string("Overture PETG Black @INDX"), generic.name}) {
        const ExtruderPlan other = with_alternative(pick, find(pick, preset));
        CHECK_FALSE(has(other.notes, "abrasive"));
        CHECK(has(other.notes, "0.60 mm nozzle"));
    }
}

TEST_CASE("An alternative of another color says so", "[FilamentSync]")
{
    const auto has = [](const std::vector<std::string> &notes, const std::string &part) {
        return std::any_of(notes.begin(), notes.end(), [&](const std::string &n) { return n.find(part) != std::string::npos; });
    };
    const std::vector<Candidate> poly = {cand("PolyLite PLA Teal", "Polymaker", "PLA", "#48B9C2"),
                                         cand("PolyTerra PLA Red", "Polymaker", "PLA", "#C0392B"),
                                         sys("Generic PLA @SYNC", "Generic", "PLA")};
    const ExtruderPlan white = plan_one(tool(1, "Polymaker", "PLA", "White", "#FFFFFF"), poly, "PolyLite PLA Teal");
    REQUIRE(white.action == Action::Create);
    REQUIRE(white.alternatives.size() == 3);
    CHECK(has(white.alternatives[0].notes, "Tool 1 has White filament; PolyLite PLA Teal is another color."));
    CHECK(has(white.alternatives[1].notes, "PolyTerra PLA Red is another color"));
    // Generic names no color
    CHECK(white.alternatives[2].notes.empty());

    // When no preset of the tool's color can be made, every other color says why
    Candidate teal = cand("PolyLite PLA-CF Teal", "Polymaker", "PLA", "#48B9C2"), red = cand("PolyTerra PLA-CF Red", "Polymaker", "PLA", "#C0392B");
    teal.is_abrasive = red.is_abrasive = true;
    const ExtruderPlan only = plan_one(tool(1, "Polymaker", "PLA", "White", "#FFFFFF"), {teal, red}, teal.name);
    REQUIRE(only.action == Action::Keep);
    CHECK(has(only.notes, "The only PLA presets are abrasive"));
    REQUIRE(only.alternatives.size() == 1);
    CHECK(has(only.alternatives[0].notes, "The only PLA presets are abrasive"));
    CHECK(has(only.alternatives[0].notes, "PolyTerra PLA-CF Red is abrasive"));
    CHECK(has(only.alternatives[0].notes, "PolyTerra PLA-CF Red is another color"));
}

TEST_CASE("Offered new presets share planned ones and don't take their names", "[FilamentSync]")
{
    // Extruder 1 keeps a preset whose color is still its parent's; extruder 2 can only start from Generic
    Candidate polymaker     = cand("Polymaker PETG", "Polymaker", "PETG", "#FF8000");
    polymaker.parent        = "Generic PETG";
    polymaker.parent_colour = "#FF8000";
    const Candidate generic = sys("Generic PETG @SYNC", "Generic", "PETG");
    Candidate       other   = polymaker;
    other.is_compatible     = false;

    PrinterTools tools;
    tools.tools     = {tool(1, "Polymaker", "PETG", "Red", "#C00000"), tool(2, "Polymaker", "PETG", "Red", "#FF0000"),
                       tool(3, "Polymaker", "PETG", "Red", "#FF0000")};
    const Plan plan = make_plan(tools, {extruder(0, {polymaker, generic}, polymaker.name), extruder(1, {other, generic}),
                                        extruder(2, {polymaker, generic}, polymaker.name)}, {}, "INDX");
    REQUIRE(plan.extruders.size() == 3);
    // The plan's new preset keeps the plain name; the one offered for another red is scoped
    REQUIRE(plan.extruders[1].action == Action::Create);
    CHECK(plan.extruders[1].preset == "Polymaker PETG Red");
    REQUIRE(plan.extruders[0].action == Action::Keep);
    REQUIRE(!plan.extruders[0].alternatives.empty());
    CHECK(plan.extruders[0].alternatives[0].preset == "Polymaker PETG Red @INDX");
    // The same red is offered the planned preset
    REQUIRE(plan.extruders[2].action == Action::Keep);
    CHECK(plan.extruders[2].alternatives[0].action == Action::Create);
    CHECK(plan.extruders[2].alternatives[0].preset == "Polymaker PETG Red");
    CHECK(plan.extruders[2].alternatives[0].template_preset == generic.name);
}

TEST_CASE("Empty tools and uncalibrated docks are unavailable, and a print using them is warned", "[FilamentSync]")
{
    PrinterTools tools;
    PrinterTool  empty = tool(6, "", "");
    empty.loaded       = false;
    // Docks 1, 2, 3, 5 and 6 calibrated on an 8-extruder profile; tool 6 empty; tool 9 beyond it
    tools.tools = {tool(1, "Overture", "PLA"), tool(2, "Overture", "PETG"), tool(3, "Overture", "ASA"), tool(5, "Prusament", "PETG"),
                   empty, tool(9, "Overture", "PLA")};
    const UnavailableExtruders u = unavailable_extruders(tools, 8);
    CHECK(u.empty == std::vector<int>{5});
    CHECK(u.uncalibrated == std::vector<int>{3, 6, 7});

    // The filament presets the print was sliced with; extruder 8 has none
    const std::vector<std::string> names = {"Overture PLA Black",          "Overture PETG White", "Overture ASA Gray",
                                            "Generic PETG",                "Prusament PETG Galaxy Black",
                                            "Overture PETG Black",         "Overture PLA Red",    ""};
    CHECK(unavailable_extruders_warning(u, {0, 1, 4}, names, "Test", "17:05").empty());
    CHECK(unavailable_extruders_warning(u, {0, 5}, names, "Test", "17:05") ==
          "Test can't print with extruder 6 (Overture PETG Black, not loaded), which this print uses (last sync at 17:05). "
          "Load the filament, or move those parts to another extruder.");
    const std::string both = unavailable_extruders_warning(u, {3, 5, 7}, names, "Test", "17:05");
    CHECK(both.find("extruder 4 (Generic PETG, dock not calibrated), extruder 6 (Overture PETG Black, not loaded) or "
                    "extruder 8 (dock not calibrated)") != std::string::npos);
    // No names passed: by number alone
    CHECK(unavailable_extruders_warning(u, {5}, {}, "Test", "17:05").find("with extruder 6 (not loaded), which") !=
          std::string::npos);
    // No dock calibrated: everything is unavailable
    CHECK(unavailable_extruders({}, 2).uncalibrated == std::vector<int>{0, 1});
}

TEST_CASE("An unreadable tool isn't an uncalibrated dock", "[FilamentSync]")
{
    PrinterTools tools;
    tools.tools      = {tool(1, "Overture", "PLA")};
    tools.unreadable = {1};
    const UnavailableExtruders u = unavailable_extruders(tools, 3);
    CHECK(u.empty.empty());
    CHECK(u.uncalibrated == std::vector<int>{2});
}

TEST_CASE("A preset that can't be written is taken out again", "[FilamentSync]")
{
    Fixture f;
    f.bundle.filaments.get_edited_preset().config.set_key_value("filament_notes", new ConfigOptionStrings({"keep me"}));
    f.bundle.filaments.update_dirty();

    PrinterTools tools;
    tools.tools     = {tool(2, "Polymaker", "PLA")};
    const Plan plan = f.plan_for(tools);
    REQUIRE(plan.extruders[0].action == Action::Create);

    const fs::path dir = f.data.dir() / "filament";
    fs::permissions(dir, fs::owner_read | fs::owner_exe);
    const ApplyResult r = apply_presets(f.bundle, &f.app, plan, {true});
    fs::permissions(dir, fs::owner_all);

    CHECK(r.created.empty());
    CHECK(r.errors.size() == 1);
    CHECK(f.bundle.filaments.find_preset("Polymaker PLA", false, false) == nullptr);
    CHECK(f.bundle.extruders_filaments[1].get_selected_preset_name() == "My PLA");
    CHECK(f.bundle.filaments.current_is_dirty());
    CHECK(f.bundle.filaments.get_edited_preset().config.opt_string("filament_notes", 0u) == "keep me");
}

TEST_CASE("A saved preset file is read back in full", "[FilamentSync]")
{
    TempDataDir data;
    DynamicPrintConfig config;
    config.set_deserialize_strict({{"filament_type", "PETG"}, {"filament_vendor", "Polymaker"}, {"filament_colour", "#0000FF"},
                                   {"temperature", "245"}, {"filament_notes", "a note"}});
    const std::string file = (data.dir() / "Polymaker PETG.ini").string();
    config.save(file);
    CHECK(saved_config_matches(file, config));

    // A disk that filled up mid-write: the file stops short
    std::string text;
    {
        boost::nowide::ifstream in(file);
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    write_file(file, text.substr(0, text.size() / 2));
    CHECK_FALSE(saved_config_matches(file, config));
    CHECK_FALSE(saved_config_matches((data.dir() / "missing.ini").string(), config));
}

TEST_CASE("A new preset without a color doesn't keep its template's", "[FilamentSync]")
{
    Fixture      f;
    PrinterTools tools;
    tools.tools     = {tool(2, "Polymaker", "PLA")};
    const Plan plan = f.plan_for(tools);
    REQUIRE(plan.extruders[0].action == Action::Create);
    // My PLA, the template, is white
    REQUIRE(plan.extruders[0].template_preset == "My PLA");
    const ApplyResult r = apply_presets(f.bundle, &f.app, plan, {true});
    REQUIRE(r.errors.empty());
    const Preset *created = f.bundle.filaments.find_preset("Polymaker PLA", false, false);
    REQUIRE(created != nullptr);
    CHECK(created->config.opt_string("filament_colour", 0u) == print_config_def.get("filament_colour")->default_value->serialize());
}

TEST_CASE("A new color of a synced filament gets its own preset", "[FilamentSync]")
{
    Fixture      f;
    PrinterTools tools;
    tools.tools = {tool(2, "Polymaker", "PLA", "Red", "#FF0000")};
    REQUIRE(apply_presets(f.bundle, &f.app, f.plan_for(tools), {true}).errors.empty());
    REQUIRE(f.bundle.extruders_filaments[1].get_selected_preset_name() == "Polymaker PLA Red");

    // The next spool on that tool is blue
    tools.tools     = {tool(2, "Polymaker", "PLA", "Blue", "#0000FF")};
    const Plan plan = f.plan_for(tools);
    REQUIRE(plan.extruders[0].action == Action::Create);
    CHECK(plan.extruders[0].preset == "Polymaker PLA Blue");
    CHECK(plan.extruders[0].template_preset == "Polymaker PLA Red");
    REQUIRE(apply_presets(f.bundle, &f.app, plan, {true}).errors.empty());
    const Preset *blue = f.bundle.filaments.find_preset("Polymaker PLA Blue", false, false);
    REQUIRE(blue != nullptr);
    CHECK(blue->config.opt_string("filament_colour", 0u) == "#0000FF");
    CHECK(f.bundle.extruders_filaments[1].get_selected_preset_name() == "Polymaker PLA Blue");
    // The red one stays for the next red spool
    CHECK(f.bundle.filaments.find_preset("Polymaker PLA Red", false, false) != nullptr);
}

TEST_CASE("A preset made from a system preset names it as its parent", "[FilamentSync]")
{
    Fixture      f;
    PrinterTools tools;
    tools.tools     = {tool(1, "Polymaker", "PETG", "Black", "#000000")};
    const Plan plan = f.plan_for(tools);
    REQUIRE(plan.extruders[0].template_preset == "Generic PETG @SYNC");
    REQUIRE(apply_presets(f.bundle, &f.app, plan, {true}).errors.empty());

    const std::vector<ExtruderState> exs = collect_extruder_states(f.bundle);
    REQUIRE(!exs.empty());
    const auto made = std::find_if(exs[0].candidates.begin(), exs[0].candidates.end(),
                                   [](const Candidate &c) { return c.name == "Polymaker PETG Black"; });
    REQUIRE(made != exs[0].candidates.end());
    CHECK(made->parent == "Generic PETG");
    CHECK(made->parent_colour == "#FF8000");
    // System presets have none
    const auto generic = std::find_if(exs[0].candidates.begin(), exs[0].candidates.end(),
                                      [](const Candidate &c) { return c.name == "Generic PETG @SYNC"; });
    REQUIRE(generic != exs[0].candidates.end());
    CHECK(generic->parent.empty());
    CHECK(generic->parent_colour.empty());
}

TEST_CASE("A preset made for a color that starts like a variant's name isn't a copy of the variant", "[FilamentSync]")
{
    // Saved from a variant the printer profile can't use, as Filament DB presets are
    Fixture f({{"Spectrum PETG Matt Black",
                "inherits = Spectrum PETG Matt @SYNC\nfilament_type = PETG\nfilament_vendor = Spectrum\nfilament_colour = #000000\n"}},
              "Spectrum PETG Matt Black");
    REQUIRE(f.bundle.extruders_filaments[0].get_selected_preset_name() == "Spectrum PETG Matt Black");

    PrinterTools tools;
    tools.tools = {tool(1, "Spectrum", "PETG", "Matt Olive", "#556B2F")};
    Plan plan   = f.plan_for(tools);
    REQUIRE(plan.extruders[0].action == Action::Create);
    REQUIRE(plan.extruders[0].preset == "Spectrum PETG Matt Olive");
    REQUIRE(apply_presets(f.bundle, &f.app, plan, {true}).errors.empty());
    const Preset *olive = f.bundle.filaments.find_preset("Spectrum PETG Matt Olive", false, false);
    REQUIRE(olive != nullptr);
    CHECK(olive->inherits() == "Generic PETG @SYNC");

    // A red spool on the next sync gets a red preset, not the olive one
    tools.tools = {tool(1, "Spectrum", "PETG", "Red", "#FF0000")};
    plan        = f.plan_for(tools);
    CHECK(plan.extruders[0].action == Action::Create);
    CHECK(plan.extruders[0].preset == "Spectrum PETG Red");
}

TEST_CASE("An offered preset is applied like a planned one", "[FilamentSync]")
{
    Fixture      f;
    PrinterTools tools;
    // My PLA is white: kept, with a new preset offered instead
    tools.tools = {tool(2, "Acme", "PLA", "White", "#FFFFFF")};
    Plan plan   = f.plan_for(tools);
    REQUIRE(plan.extruders[0].action == Action::Keep);
    REQUIRE(!plan.extruders[0].alternatives.empty());
    REQUIRE(plan.extruders[0].alternatives[0].action == Action::Create);
    plan.extruders[0] = with_alternative(plan.extruders[0], 0);
    REQUIRE(plan.extruders[0].preset == "Acme PLA White");

    const ApplyResult r = apply_presets(f.bundle, &f.app, plan, {true});
    REQUIRE(r.errors.empty());
    CHECK(r.created == std::vector<std::string>{"Acme PLA White"});
    CHECK(f.bundle.extruders_filaments[1].get_selected_preset_name() == "Acme PLA White");
    const Preset *made = f.bundle.filaments.find_preset("Acme PLA White", false, false);
    REQUIRE(made != nullptr);
    CHECK(made->config.opt_string("filament_colour", 0u) == "#FFFFFF");
}
