///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "FilamentSync.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {
namespace filament_sync {

namespace {

std::string lower(const std::string &s)
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return out;
}

// Lowercased alphanumeric words; "grey" is spelled "gray" like the printer's color.
std::vector<std::string> tokens(const std::string &s)
{
    std::vector<std::string> out;
    std::string              word;
    for (unsigned char c : s + ' ') {
        if (std::isalnum(c)) {
            word += char(std::tolower(c));
        } else if (!word.empty()) {
            out.emplace_back(word == "grey" ? "gray" : word);
            word.clear();
        }
    }
    return out;
}

bool contains_sequence(const std::vector<std::string> &haystack, const std::vector<std::string> &needle)
{
    return !needle.empty() && std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end()) != haystack.end();
}

// The printer's preset color names (Prusa-Firmware-Buddy filament_library.cpp).
bool is_colour_word(const std::string &w)
{
    static const std::set<std::string> words = {"red",    "orange", "green", "brown",   "blue",    "gray",
                                                "cyan",   "purple", "magenta", "pink",  "yellow",  "silver",
                                                "black",  "natural", "white", "gold"};
    return words.count(w) > 0;
}

// The printer's unnamed user colors COLOR1..COLOR8 say nothing about the filament.
std::vector<std::string> colour_words(const std::string &color)
{
    const std::string l = lower(color);
    if (l.size() == 6 && l.compare(0, 5, "color") == 0 && l[5] >= '1' && l[5] <= '8')
        return {};
    return tokens(color);
}

// The name a candidate is shown with, without a printer scope after "@".
std::string display_name(const Candidate &c)
{
    std::string s = c.alias.empty() ? c.name : c.alias;
    if (const size_t at = s.find('@'); at != std::string::npos)
        s = s.substr(0, at);
    return s;
}

int rgb_distance(const std::string &a, const std::string &b)
{
    const std::string na = printer_tools_detail::normalize_color_hex(a);
    const std::string nb = printer_tools_detail::normalize_color_hex(b);
    if (na.empty() || nb.empty())
        return std::numeric_limits<int>::max();
    int d = 0;
    for (int i = 1; i < 7; i += 2) {
        const int x = std::stoi(na.substr(i, 2), nullptr, 16) - std::stoi(nb.substr(i, 2), nullptr, 16);
        d += x * x;
    }
    return d;
}

bool is_fiber_filled(const std::string &name)
{
    const std::vector<std::string> t = tokens(name);
    for (size_t i = 0; i < t.size(); ++i) {
        const std::string &w = t[i];
        if ((w.size() >= 2 && (w.compare(0, 2, "cf") == 0 || w.compare(0, 2, "gf") == 0) &&
             std::all_of(w.begin() + 2, w.end(), [](char c) { return std::isdigit((unsigned char) c); })))
            return true;
        if ((w == "carbon" || w == "glass") && i + 1 < t.size() && (t[i + 1] == "fiber" || t[i + 1] == "fibre"))
            return true;
    }
    return false;
}

// What the tool asks for, prepared once.
struct Wanted
{
    const PrinterTool       &tool;
    bool                     vendor_known;
    std::string              vendor_key;
    std::vector<std::string> colour;     // color words, may be empty
    std::set<std::string>    plain_words; // vendor, material and color words of the tool
    std::string              material_key; // the material's letters and digits, lowercase

    explicit Wanted(const PrinterTool &t)
        : tool(t)
        , vendor_known(!is_placeholder_vendor(t.vendor))
        , vendor_key(filament_sync::vendor_key(t.vendor))
        , colour(colour_words(t.color))
    {
        for (const auto &w : tokens(t.vendor)) plain_words.insert(w);
        for (const auto &w : tokens(t.material)) {
            plain_words.insert(w);
            material_key += w;
        }
        for (const auto &w : colour) plain_words.insert(w);
    }
};

// Whether a name is "<vendor> <material> <more words>", the vendor and material spelled any way
// the candidates match ("BambuLab" for "Bambu Lab", "TPE" for "TPU")
bool names_more_than_filament(const Wanted &want, const std::vector<std::string> &words)
{
    std::string vendor;
    for (size_t i = 0; i + 2 < words.size(); ++i) {
        vendor += words[i];
        if (filament_sync::vendor_key(vendor) != want.vendor_key)
            continue;
        std::string material;
        for (size_t j = i + 1; j + 1 < words.size(); ++j) {
            material += words[j];
            // The printer keeps 7 characters of a type's name, as in same_material()
            const bool cut = want.tool.material.size() == 7 && material.size() > want.material_key.size() &&
                             material.compare(0, want.material_key.size(), want.material_key) == 0;
            if (same_material(want.material_key, material) || cut)
                return true;
        }
    }
    return false;
}

// The "#RRGGBB" in a name, as in the presets made for the printer's unnamed user colors
std::string hex_in_name(const std::string &name)
{
    for (size_t at = name.find('#'); at != std::string::npos; at = name.find('#', at + 1))
        if (at + 7 == name.size() || (at + 7 < name.size() && !std::isalnum((unsigned char) name[at + 7])))
            if (std::string hex = printer_tools_detail::normalize_color_hex(name.substr(at, 7)); !hex.empty())
                return hex;
    return {};
}

// 0: the tool's color, by hex or by name. 1: no color in the name. 2: another color.
int colour_tier(const Wanted &want, const Candidate &c)
{
    // A tool without a color says nothing against any preset
    if (want.colour.empty() && want.tool.color_hex.empty())
        return 1;
    const std::string              name  = display_name(c);
    const std::vector<std::string> words = tokens(name);
    // A hex in the name is the color, whatever the preset's own color was edited to since
    if (const std::string named = hex_in_name(name); !named.empty() && !want.tool.color_hex.empty())
        return named == printer_tools_detail::normalize_color_hex(want.tool.color_hex) ? 0 : 2;
    if ((!want.tool.color_hex.empty() && rgb_distance(c.colour, want.tool.color_hex) == 0) || contains_sequence(words, want.colour))
        return 0;
    for (const std::string &w : words)
        if (is_colour_word(w) && std::find(want.colour.begin(), want.colour.end(), w) == want.colour.end())
            return 2;
    // A user preset named "<vendor> <material> <color>", as new presets are: what follows the
    // material is a color even when the printer's list doesn't have it (a user color named
    // "Clear"), and the preset's own color differs. In a system preset it names a variant
    // ("Prusament PETG Carbon Fiber"), which the printer can't tell apart, and so it does in a
    // copy saved from the variant under its name ("Prusament PLA Blend Tuned"). A new preset
    // inherits its template, whose name its own doesn't continue.
    const auto copies_variant = [&] {
        const std::vector<std::string> parent = tokens(c.parent);
        return names_more_than_filament(want, parent) && words.size() >= parent.size() &&
               std::equal(parent.begin(), parent.end(), words.begin());
    };
    if (!c.is_system && want.vendor_known && !want.tool.color_hex.empty() &&
        !printer_tools_detail::normalize_color_hex(c.colour).empty() && names_more_than_filament(want, words) && !copies_variant())
        return 2;
    return 1;
}

// Words of the name beyond vendor, material and color: "Overture PLA White" says less than
// "Overture PLA Matte White", so it is the closer match.
int extra_words(const Wanted &want, const Candidate &c)
{
    std::set<std::string> plain = want.plain_words;
    for (const auto &w : tokens(c.vendor)) plain.insert(w);
    for (const auto &w : tokens(c.type)) plain.insert(w);
    if (vendor_key(c.vendor) == "prusament")
        for (const char *w : {"prusament", "prusa", "polymers"}) plain.insert(w);
    int n = 0;
    for (const std::string &w : tokens(display_name(c)))
        if (!plain.count(w))
            ++n;
    return n;
}

int vendor_preference(const Candidate &c)
{
    const std::string k = vendor_key(c.vendor);
    if (k == "generic")
        return 0;
    if (k == "prusament")
        return 1;
    return c.is_system ? 2 : 3;
}

// Lower is better. The first two entries are what the printer can tell apart (abrasive and
// color); the rest only orders presets that fit equally well.
using Rank = std::tuple<int, int, int, int, int, std::string>;

Rank rank(const Wanted &want, const Candidate &c)
{
    const int abrasive = (!want.tool.hardened && c.is_abrasive) ? 1 : 0;
    const int tier     = colour_tier(want, c);
    if (want.vendor_known)
        return {abrasive, tier, extra_words(want, c), rgb_distance(c.colour, want.tool.color_hex), c.is_system ? 1 : 0, c.name};
    return {abrasive, tier, vendor_preference(c), extra_words(want, c), rgb_distance(c.colour, want.tool.color_hex), c.name};
}

// The printer reports vendor, material and color, not a variant such as matte, silk or carbon
// fiber, so a selected preset that fits as well as the best one is the user's choice to keep.
bool fits_as_well(const Rank &a, const Rank &b)
{
    return std::get<0>(a) == std::get<0>(b) && std::get<1>(a) == std::get<1>(b);
}

bool fits_better(const Rank &a, const Rank &b)
{
    return std::make_pair(std::get<0>(a), std::get<1>(a)) < std::make_pair(std::get<0>(b), std::get<1>(b));
}

const Candidate *best_of(const Wanted &want, const std::vector<const Candidate *> &set)
{
    const Candidate *best = nullptr;
    Rank             best_rank;
    for (const Candidate *c : set) {
        Rank r = rank(want, *c);
        if (!best || r < best_rank) {
            best      = c;
            best_rank = std::move(r);
        }
    }
    return best;
}

std::string mm(double d) { return (boost::format("%.2f") % d).str(); }

void nozzle_notes(const PrinterTool &tool, const ExtruderState &ex, std::vector<std::string> &notes)
{
    const int n = tool.extruder_idx + 1;
    if (std::abs(tool.nozzle_diameter - ex.nozzle_diameter) > 0.005)
        notes.emplace_back((boost::format("Tool %1% has a %2% mm nozzle; the printer profile has %3% mm.") % n %
                            mm(tool.nozzle_diameter) % mm(ex.nozzle_diameter)).str());
    if (tool.high_flow != ex.high_flow)
        notes.emplace_back((boost::format("Tool %1% has a %2% nozzle; the printer profile has a %3% one.") % n %
                            (tool.high_flow ? "high flow" : "standard flow") % (ex.high_flow ? "high flow" : "standard flow")).str());
}

// Lowercased names of the .ini files in a preset folder; the filesystem may tell case apart, the
// preset names don't.
std::set<std::string> ini_stems(const PresetCollection &presets)
{
    namespace fs = boost::filesystem;
    std::set<std::string>     out;
    const fs::path            dir = fs::path(presets.path_from_name("probe")).parent_path();
    boost::system::error_code ec;
    if (!dir.empty() && fs::is_directory(dir, ec))
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            if (boost::iequals(it->path().extension().string(), ".ini"))
                out.insert(lower(it->path().stem().string()));
    return out;
}

// PrusaSlicer's filament_colour default, "#RRGGBB": a new preset's color when the tool has none.
std::string default_filament_colour()
{
    const ConfigOptionDef *def = print_config_def.get("filament_colour");
    return def && def->default_value ? printer_tools_detail::normalize_color_hex(def->default_value->serialize()) : std::string();
}

// Keyed by vendor and material: several tools with the same filament share one new preset.
struct PendingCreate
{
    std::string name;
    std::string template_preset;
};

} // namespace

std::string vendor_key(const std::string &vendor)
{
    std::string k;
    for (unsigned char c : vendor)
        if (std::isalnum(c))
            k += char(std::tolower(c));
    return k == "prusapolymers" ? "prusament" : k;
}

bool is_placeholder_vendor(const std::string &vendor)
{
    const std::string k = vendor_key(vendor);
    return k.empty() || (k.size() == 7 && k.compare(0, 6, "vendor") == 0 && k[6] >= '1' && k[6] <= '8');
}

bool same_material(const std::string &tool_material, const std::string &preset_type)
{
    const std::string a = lower(tool_material), b = lower(preset_type);
    if (a.empty() || b.empty())
        return false;
    if (a == b)
        return true;
    static const std::set<std::string> flexible = {"flex", "tpu", "tpe", "peba"};
    if (flexible.count(a) && flexible.count(b))
        return true;
    // The printer keeps 7 characters of a type's name (filament.hpp, filament_name_buffer_size).
    return a.size() == 7 && b.size() > 7 && b.compare(0, 7, a) == 0;
}

std::string check_new_preset_name(const std::string &name)
{
    if (name.empty())
        return "the name is empty";
    // Names sorting before "- default -" break the filament lists' binary search.
    if (!std::isalnum((unsigned char) name.front()))
        return "a preset name has to start with a letter or digit";
    if (std::isspace((unsigned char) name.back()))
        return "a preset name can't end with a space";
    for (const char c : std::string("<>[]:/\\|?*\""))
        if (name.find(c) != std::string::npos)
            return std::string("a preset name can't contain ") + c;
    if (boost::algorithm::ends_with(name, Preset::suffix_modified()))
        return "a preset name can't end with \"" + Preset::suffix_modified() + "\"";
    if (name.size() > 200)
        return "the name is too long";
    return {};
}

Plan make_plan(const PrinterTools &tools, const std::vector<ExtruderState> &extruders,
               const std::set<std::string> &taken_names, const std::string &name_tag)
{
    Plan plan;
    for (const std::string &w : tools.warnings)
        plan.notes.emplace_back("The printer sent " + w + ".");

    std::set<std::string>                  taken = taken_names;
    // By vendor_key + '\n' + lowercased material: one per template, as compatibility is per extruder
    std::map<std::string, std::vector<PendingCreate>> creates;

    for (const PrinterTool &tool : tools.tools) {
        if (tool.extruder_idx < 0 || size_t(tool.extruder_idx) >= extruders.size()) {
            plan.notes.emplace_back((boost::format("Tool %1% isn't in the printer profile, which has %2% extruders.") %
                                     (tool.extruder_idx + 1) % extruders.size()).str());
            continue;
        }
        const ExtruderState &ex = extruders[tool.extruder_idx];
        ExtruderPlan         row;
        row.idx  = tool.extruder_idx;
        row.tool = tool;
        if (!tool.loaded) {
            plan.extruders.emplace_back(std::move(row));
            continue;
        }

        const Wanted want(tool);
        const int    n = tool.extruder_idx + 1;
        if (!want.vendor_known)
            row.notes.emplace_back(tool.vendor.empty() ?
                (boost::format("The printer reports no vendor for tool %1%.") % n).str() :
                (boost::format("Vendor \"%1%\" on tool %2% is one of the printer's unnamed user vendors.") % tool.vendor % n).str());
        nozzle_notes(tool, ex, row.notes);

        // Usable for this tool: compatible with the extruder and of the same material
        std::vector<const Candidate *> visible, invisible_system;
        for (const Candidate &c : ex.candidates) {
            if (!c.is_compatible || !same_material(tool.material, c.type))
                continue;
            if (want.vendor_known && vendor_key(c.vendor) != want.vendor_key)
                continue;
            if (c.is_visible)
                visible.push_back(&c);
            else if (c.is_system)
                invisible_system.push_back(&c);
        }

        const Candidate *pick = best_of(want, visible);
        if (pick) {
            const auto current = std::find_if(visible.begin(), visible.end(), [&](const Candidate *c) { return c->name == ex.selected; });
            if (current != visible.end() && fits_as_well(rank(want, **current), rank(want, *pick)))
                pick = *current;
        }
        // A system preset that isn't installed, when nothing installed fits or only another color does
        bool install = false;
        if (const Candidate *hidden = best_of(want, invisible_system);
            hidden && (!pick || (colour_tier(want, *pick) == 2 && fits_better(rank(want, *hidden), rank(want, *pick))))) {
            pick    = hidden;
            install = true;
        }

        // A known vendor's filament whose presets are all another color gets one of its own
        const PendingCreate *create = nullptr;
        std::string          failure;
        if (want.vendor_known && (!pick || colour_tier(want, *pick) == 2)) {
            // The presets carry the colors, so tools with different colors don't share one: by hex,
            // else by color name
            std::string colour_key = tool.color_hex;
            if (colour_key.empty())
                for (const std::string &w : want.colour)
                    colour_key += ' ' + w;
            const std::string key = want.vendor_key + '\n' + lower(tool.material) + '\n' + colour_key;
            // A template this tool can use: compatible with its extruder, of its material, and not
            // abrasive on a nozzle that isn't hardened
            const auto usable_template = [&](const std::string &name) {
                return std::any_of(ex.candidates.begin(), ex.candidates.end(), [&](const Candidate &c) {
                    return c.name == name && c.is_compatible && same_material(tool.material, c.type) &&
                           (tool.hardened || !c.is_abrasive) && (c.is_visible || c.is_system);
                });
            };
            // Share a preset another tool creates when its template suits this extruder too
            std::vector<PendingCreate> &pending = creates[key];
            const auto shared = std::find_if(pending.begin(), pending.end(),
                                             [&](const PendingCreate &p) { return usable_template(p.template_preset); });
            if (shared != pending.end())
                create = &*shared;
            else {
                // Start from a preset of the same material: the extruder's own, else Generic,
                // Prusament, another vendor's, a user preset; installed ones first
                std::vector<const Candidate *> templates;
                bool                            abrasive_only = false;
                for (bool visible_pass : {true, false}) {
                    for (const Candidate &c : ex.candidates) {
                        if (!c.is_compatible || !same_material(tool.material, c.type) || c.is_visible != visible_pass ||
                            (!visible_pass && !c.is_system))
                            continue;
                        if (!tool.hardened && c.is_abrasive) {
                            abrasive_only = true;
                            continue;
                        }
                        templates.push_back(&c);
                    }
                    if (!templates.empty())
                        break;
                }
                const auto template_rank = [&](const Candidate *c) {
                    return std::make_tuple(c->name == ex.selected ? 0 : 1, vendor_preference(*c), c->name);
                };
                const Candidate *tmpl = templates.empty() ? nullptr :
                    *std::min_element(templates.begin(), templates.end(),
                                      [&](const Candidate *a, const Candidate *b) { return template_rank(a) < template_rank(b); });

                // Named like a per-color preset: "Polymaker PETG Black"; an unnamed user color by its hex
                const std::string colour = !want.colour.empty() ? tool.color : tool.color_hex;
                const std::string base   = tool.vendor + " " + tool.material + (colour.empty() ? "" : " " + colour);
                std::string       name   = base;
                if (taken.count(lower(name)))
                    name += " @" + name_tag;
                for (int i = 2; taken.count(lower(name)); ++i)
                    name = base + " @" + name_tag + " " + std::to_string(i);

                if (!tmpl) {
                    failure = abrasive_only ?
                        (boost::format("The only %1% presets are abrasive, and tool %2%'s nozzle isn't hardened.") % tool.material % n).str() :
                        (boost::format("There is no %1% preset for this printer to start a %2% preset from.") % tool.material % tool.vendor).str();
                } else if (const std::string bad = check_new_preset_name(name); !bad.empty()) {
                    failure = "Can't name a preset \"" + name + "\": " + bad + ".";
                } else {
                    taken.insert(lower(name));
                    pending.push_back(PendingCreate{name, tmpl->name});
                    create = &pending.back();
                }
            }
        }

        if (create) {
            row.action          = Action::Create;
            row.preset          = create->name;
            row.template_preset = create->template_preset;
            row.vendor          = tool.vendor;
            pick                = nullptr;
        } else if (pick) {
            row.action = install ? Action::Install : pick->name == ex.selected ? Action::Keep : Action::Select;
            row.preset = pick->name;
            // Only another color fits, and a preset of the tool's own couldn't be made
            if (!failure.empty())
                row.notes.emplace_back(failure);
        } else {
            row.action = Action::NoMatch;
            row.notes.emplace_back(want.vendor_known ? failure :
                                                       (boost::format("No %1% preset fits this printer.") % tool.material).str());
        }

        if (pick) {
            if (!tool.hardened && pick->is_abrasive)
                row.notes.emplace_back((boost::format("%1% is abrasive, and tool %2%'s nozzle isn't hardened.") % display_name(*pick) % n).str());
            if (colour_tier(want, *pick) == 2)
                row.notes.emplace_back((boost::format("No %1% preset is %2%; %3% is the closest.") % tool.material %
                                        (want.colour.empty() ? tool.color_hex : tool.color) % pick->name).str());
        }
        if (row.action == Action::Create)
            row.preset_colour = tool.color_hex.empty() ? default_filament_colour() : tool.color_hex;
        else if (pick)
            row.preset_colour = printer_tools_detail::normalize_color_hex(pick->colour);
        plan.extruders.emplace_back(std::move(row));
    }
    return plan;
}

std::vector<ExtruderState> collect_extruder_states(const PresetBundle &bundle)
{
    // The extruder count and the per-extruder filament lists follow the edited printer preset
    const DynamicPrintConfig &printer   = bundle.printers.get_edited_preset().config;
    const auto               *diameters = printer.option<ConfigOptionFloats>("nozzle_diameter");
    const auto               *high_flow = printer.option<ConfigOptionBools>("nozzle_high_flow");

    std::vector<ExtruderState> out;
    for (size_t i = 0; i < bundle.extruders_filaments.size(); ++i) {
        const ExtruderFilaments &ef = bundle.extruders_filaments[i];
        ExtruderState            ex;
        ex.idx             = int(i);
        ex.selected        = ef.get_selected_preset_name();
        ex.nozzle_diameter = diameters && !diameters->values.empty() ? diameters->get_at(i) : 0.;
        ex.high_flow       = high_flow && !high_flow->values.empty() ? high_flow->get_at(i) : false;
        for (const Filament &f : ef) {
            const Preset *p = f.preset;
            if (!p || p->is_default || p->is_external || (p->vendor && p->vendor->templates_profile))
                continue;
            Candidate c;
            c.name          = p->name;
            c.alias         = p->alias;
            if (!p->is_system)
                if (const Preset *parent = bundle.filaments.get_preset_parent(*p); parent && parent->is_system) {
                    Candidate pc;
                    pc.name  = parent->name;
                    pc.alias = parent->alias;
                    c.parent = display_name(pc);
                }
            c.vendor        = p->config.has("filament_vendor") ? p->config.opt_string("filament_vendor") : std::string();
            c.type          = p->config.has("filament_type") ? p->config.opt_string("filament_type", 0u) : std::string();
            c.colour        = p->config.has("filament_colour") ? p->config.opt_string("filament_colour", 0u) : std::string();
            c.is_system     = p->is_system;
            c.is_visible    = p->is_visible;
            c.is_compatible = f.is_compatible;
            c.is_abrasive   = (p->config.has("filament_abrasive") && p->config.opt_bool("filament_abrasive", 0u)) ||
                              is_fiber_filled(p->name);
            ex.candidates.emplace_back(std::move(c));
        }
        out.emplace_back(std::move(ex));
    }
    return out;
}

std::set<std::string> taken_filament_names(PresetCollection &filaments)
{
    std::set<std::string> out;
    for (const Preset &p : filaments()) {
        out.insert(lower(p.name));
        if (!p.alias.empty())
            out.insert(lower(p.alias));
    }
    for (const auto &[alias, name] : filaments.map_alias_to_profile_name())
        if (!alias.empty())
            out.insert(lower(alias));
    // Files load_presets() skipped or couldn't read still own their name on disk
    for (const std::string &stem : ini_stems(filaments))
        out.insert(stem);
    return out;
}

std::string printer_name_tag(const PresetBundle &bundle)
{
    const DynamicPrintConfig &printer = bundle.printers.get_edited_preset().config;
    const std::string model = printer.has("printer_model") ? printer.opt_string("printer_model") : std::string();
    if (model.find("INDX") != std::string::npos)
        return "INDX";
    return model.empty() ? std::string("printer") : model;
}

ApplyResult apply_presets(PresetBundle &bundle, AppConfig *app_config, const Plan &plan, const std::vector<bool> &accepted)
{
    ApplyResult r;
    if (accepted.size() != plan.extruders.size()) {
        r.errors.emplace_back("The selection doesn't match the plan.");
        return r;
    }
    PresetCollection &fil = bundle.filaments;
    namespace fs          = boost::filesystem;

    // Inserting a preset reselects the collection's selected one with force, which drops unsaved
    // edits of the edited preset: keep them and put them back
    const bool         had_edits = fil.current_is_dirty();
    DynamicPrintConfig edits;
    if (had_edits)
        edits = fil.get_edited_preset().config;

    const auto is_accepted = [&](size_t i, Action a) { return accepted[i] && plan.extruders[i].action == a; };

    std::set<std::string> failed;   // names whose create or install failed: don't select them
    std::set<std::string> done;
    bool                  inserted = false;
    bundle.cache_extruder_filaments_names();

    for (size_t i = 0; i < plan.extruders.size(); ++i) {
        if (!is_accepted(i, Action::Create))
            continue;
        const ExtruderPlan &row = plan.extruders[i];
        if (done.count(row.preset) || failed.count(row.preset))
            continue;
        const auto fail = [&](const std::string &why) {
            r.errors.emplace_back("Couldn't create \"" + row.preset + "\": " + why + ".");
            failed.insert(row.preset);
        };
        if (const std::string bad = check_new_preset_name(row.preset); !bad.empty()) {
            fail(bad);
            continue;
        }
        const bool name_taken = std::any_of(fil().begin(), fil().end(), [&](const Preset &p) {
            return boost::iequals(p.name, row.preset) || (!p.alias.empty() && boost::iequals(p.alias, row.preset));
        });
        if (name_taken || ini_stems(fil).count(lower(row.preset))) {
            fail("a preset or file of that name exists");
            continue;
        }
        // Looked up right before the insert: inserting moves presets around in the collection
        const Preset *tmpl = fil.find_preset(row.template_preset, /* first_visible_if_not_found */ false,
                                             /* respect_active_preset */ false);
        if (!tmpl) {
            fail("its template \"" + row.template_preset + "\" is gone");
            continue;
        }
        Preset &p = fil.get_preset_with_name(row.preset, tmpl);
        inserted  = true;
        if (p.is_default || p.is_system || p.is_external || p.name != row.preset) {
            fail("the name belongs to another preset");
            continue;
        }
        p.config.opt_string("filament_vendor", true) = row.vendor;
        // The tool's color (tools sharing a new preset share it); without one, not the template's
        // unrelated color but PrusaSlicer's default
        p.config.option<ConfigOptionStrings>("filament_colour", true)->values = {
            row.tool.color_hex.empty() ? default_filament_colour() : row.tool.color_hex};
        p.save();
        if (!saved_config_matches(p.file, p.config)) {
            const std::string file = p.file;
            // Not on disk, so gone at the next start: take it out of the list now. This reselects
            // with force; the edits are put back below.
            fil.delete_preset(row.preset);
            fail("couldn't write " + file);
            continue;
        }
        BOOST_LOG_TRIVIAL(info) << "Filament sync: created " << p.name << " from " << row.template_preset;
        done.insert(row.preset);
        r.created.push_back(row.preset);
    }

    for (size_t i = 0; i < plan.extruders.size(); ++i) {
        if (!is_accepted(i, Action::Install))
            continue;
        const ExtruderPlan &row = plan.extruders[i];
        if (done.count(row.preset) || failed.count(row.preset))
            continue;
        Preset *sp = fil.find_preset(row.preset, false, false);
        if (!sp || !sp->is_system) {
            r.errors.emplace_back("Couldn't install \"" + row.preset + "\": it isn't a system preset.");
            failed.insert(row.preset);
            continue;
        }
        if (app_config)
            app_config->set(AppConfig::SECTION_FILAMENTS, row.preset, "1");
        sp->is_visible = true;
        inserted       = true;
        done.insert(row.preset);
        r.installed.push_back(row.preset);
    }

    if (inserted) {
        bundle.reset_extruder_filaments();
        bundle.update_filaments_compatible(PresetSelectCompatibleType::Never);
    }
    if (had_edits && fil.get_edited_preset().config != edits) {
        fil.get_edited_preset().config = std::move(edits);
        fil.update_dirty();
    }

    for (size_t i = 0; i < plan.extruders.size(); ++i) {
        const ExtruderPlan &row = plan.extruders[i];
        if (!accepted[i] || (row.action != Action::Select && row.action != Action::Install && row.action != Action::Create) ||
            failed.count(row.preset))
            continue;
        if (row.idx < 0 || size_t(row.idx) >= bundle.extruders_filaments.size()) {
            r.errors.emplace_back((boost::format("Extruder %1% isn't in the printer profile.") % (row.idx + 1)).str());
            continue;
        }
        ExtruderFilaments &ef = bundle.extruders_filaments[row.idx];
        if (ef.get_selected_preset_name() == row.preset)
            continue;
        // Falls back to the first visible preset when the name can't be selected: check
        ef.select_filament(row.preset);
        if (ef.get_selected_preset_name() != row.preset)
            r.errors.emplace_back((boost::format("Couldn't select \"%1%\" for extruder %2%.") % row.preset % (row.idx + 1)).str());
        else
            r.selected.push_back(row.idx);
    }
    return r;
}

UnavailableExtruders unavailable_extruders(const PrinterTools &tools, size_t extruder_count)
{
    UnavailableExtruders out;
    std::vector<bool>    reported(extruder_count, false);
    for (const PrinterTool &t : tools.tools) {
        if (t.extruder_idx < 0 || size_t(t.extruder_idx) >= extruder_count)
            continue;
        reported[t.extruder_idx] = true;
        if (!t.loaded)
            out.empty.push_back(t.extruder_idx);
    }
    // Listed but unreadable: the dock is there, its state unknown
    for (const int idx : tools.unreadable)
        if (idx >= 0 && size_t(idx) < extruder_count)
            reported[idx] = true;
    for (size_t i = 0; i < extruder_count; ++i)
        if (!reported[i])
            out.uncalibrated.push_back(int(i));
    return out;
}

std::string unavailable_extruders_warning(const UnavailableExtruders &unavailable, const std::vector<unsigned int> &used,
                                          const std::vector<std::string> &filament_names, const std::string &printer_name,
                                          const std::string &sync_time)
{
    std::vector<std::string> items;
    for (const unsigned int e : used) {
        const auto  has   = [e](const std::vector<int> &v) { return std::find(v.begin(), v.end(), int(e)) != v.end(); };
        const char *state = has(unavailable.empty) ? "not loaded" : has(unavailable.uncalibrated) ? "dock not calibrated" : nullptr;
        if (state == nullptr)
            continue;
        // Not the pickers' "2 Overture PETG Black": in a sentence, that reads as a count
        const std::string name = e < filament_names.size() ? filament_names[e] : std::string();
        items.push_back("extruder " + std::to_string(e + 1) + " (" + (name.empty() ? "" : name + ", ") + state + ")");
    }
    if (items.empty())
        return {};
    std::string list = items.front();
    for (size_t i = 1; i < items.size(); ++i)
        list += (i + 1 == items.size() ? " or " : ", ") + items[i];
    return printer_name + " can't print with " + list + ", which this print uses (last sync at " + sync_time +
           "). Load the filament, or move those parts to another extruder.";
}

bool saved_config_matches(const std::string &file, const DynamicPrintConfig &config)
{
    boost::system::error_code ec;
    if (!boost::filesystem::exists(file, ec))
        return false;
    try {
        DynamicPrintConfig saved;
        saved.load(file, ForwardCompatibilitySubstitutionRule::Disable);
        for (const std::string &key : config.keys())
            if (!saved.has(key) || saved.opt_serialize(key) != config.opt_serialize(key))
                return false;
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

} // namespace filament_sync
} // namespace Slic3r
