///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
// Reading Prusa Connect's printer list into PrinterTools. Only the JSON and the standard library,
// so the parsing is tested without the rest of the GUI library.
#include "PrinterTools.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>

#include <nlohmann/json.hpp>

namespace Slic3r {

namespace {

using json = nlohmann::json;

bool iequals(const std::string &a, const std::string &b) {
    return a.size() == b.size() &&
        std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
               return std::tolower(x) == std::tolower(y);
           });
}

// The string at a path of object keys, or "" when any step is missing or of another type.
std::string string_at(const json &node, std::initializer_list<const char *> path) {
    const json *cur = &node;
    for (const char *key : path) {
        if (!cur->is_object())
            return {};
        const auto it = cur->find(key);
        if (it == cur->end())
            return {};
        cur = &*it;
    }
    return cur->is_string() ? cur->get<std::string>() : std::string();
}

// Connect numbers a printer's tools from "0"; three digits leave room without risking overflow.
bool parse_tool_index(const std::string &key, int &index) {
    if (key.empty() || key.size() > 3 || (key.size() > 1 && key[0] == '0'))
        return false;
    const auto [ptr, ec] = std::from_chars(key.data(), key.data() + key.size(), index);
    return ec == std::errc{} && ptr == key.data() + key.size() && index >= 0;
}

// The tools object of one printer. Keys with a dot are an MMU's slots ("0.0").
void parse_connect_tools(const json &tools, PrinterTools &out, bool &has_feeder_slots) {
    for (const auto &item : tools.items()) {
        const std::string &key = item.key();
        const json &tool = item.value();
        if (key.find('.') != std::string::npos) {
            has_feeder_slots = true;
            continue;
        }
        int index = 0;
        if (!parse_tool_index(key, index)) {
            out.warnings.emplace_back("tool \"" + key + "\": not a tool number, skipped");
            continue;
        }
        // Counted from 1 like the extruders, though Connect's keys count from 0
        const std::string tool_label = "tool " + std::to_string(index + 1);
        const auto skip_tool = [&](const std::string &why) {
            out.warnings.emplace_back(tool_label + ": " + why + ", skipped");
            out.unreadable.push_back(index);
        };
        if (!tool.is_object()) {
            skip_tool("not an object");
            continue;
        }

        PrinterTool t;
        t.extruder_idx = index;

        const json *features = nullptr;
        if (const auto f = tool.find("features"); f != tool.end() && f->is_object())
            features = &*f;
        const json *diameter = nullptr;
        if (features)
            if (const auto d = features->find("nozzle_diameter");
                d != features->end() && d->is_number())
                diameter = &*d;
        if (!diameter) {
            skip_tool("\"features.nozzle_diameter\" missing or not a number");
            continue;
        }
        t.nozzle_diameter = diameter->get<double>();
        bool ok = true;
        for (auto [name, field] :
             {std::pair<const char *, bool *>{"nozzle_hardened", &t.hardened},
              {"nozzle_high_flow", &t.high_flow}}) {
            const auto it = features->find(name);
            if (it == features->end() || it->is_null())
                continue;
            if (!it->is_boolean()) {
                skip_tool(std::string("\"features.") + name + "\" isn't true/false");
                ok = false;
                break;
            }
            *field = it->get<bool>();
        }
        if (!ok)
            continue;

        // An empty tool has a null material_package_instance (or none)
        const auto mpi = tool.find("material_package_instance");
        t.loaded = mpi != tool.end() && mpi->is_object();
        if (t.loaded) {
            const json material = [&]() {
                const auto package = mpi->find("package");
                if (package == mpi->end() || !package->is_object())
                    return json::object();
                const auto m = package->find("material");
                return m != package->end() && m->is_object() ? *m : json::object();
            }();
            t.material = string_at(material, {"type"});
            if (t.material.empty())
                t.material = string_at(material, {"abbreviation"});
            if (t.material.empty()) {
                skip_tool("loaded but no material type");
                continue;
            }
            t.vendor = string_at(material, {"brand", "name"});
            t.product = string_at(material, {"name"});

            const std::string rgba = string_at(material, {"primary_color", "color_rgba"});
            t.color_hex = printer_tools_detail::normalize_color_rgba(rgba);
            if (t.color_hex.empty() && !rgba.empty())
                out.warnings.emplace_back(
                    tool_label + ": color \"" + rgba + "\" isn't #RRGGBB or #RRGGBBAA, ignored"
                );
            t.color = printer_tools_detail::printer_colour(t.product, t.color_hex);
        }
        out.tools.emplace_back(std::move(t));
    }
    std::sort(out.tools.begin(), out.tools.end(), [](const PrinterTool &a, const PrinterTool &b) {
        return a.extruder_idx < b.extruder_idx;
    });
    std::sort(out.unreadable.begin(), out.unreadable.end());
}

} // namespace

bool connect_printer_fits(const ConnectPrinter &printer, const std::string &printer_model) {
    if (printer_model.empty())
        return false;
    if (iequals(printer.legacy_model, printer_model))
        return true;
    return std::any_of(
        printer.supported_models.begin(), printer.supported_models.end(),
        [&](const std::string &m) { return iequals(m, printer_model); }
    );
}

namespace printer_tools_detail {

std::string normalize_color_rgba(const std::string &rgba) {
    if ((rgba.size() != 7 && rgba.size() != 9) || rgba[0] != '#')
        return {};
    std::string out = "#";
    for (size_t i = 1; i < rgba.size(); ++i) {
        const unsigned char c = rgba[i];
        if (!std::isxdigit(c))
            return {};
        if (i < 7)
            out += char(std::toupper(c));
    }
    return out;
}

std::string printer_colour(const std::string &product, const std::string &color_hex) {
    // The printer's preset colors as it spells and reports them (hyiger/Prusa-Firmware-Buddy
    // filament_library.cpp, preset_colors)
    struct PaletteColour
    {
        const char *name;
        const char *hex;
    };
    static const std::array<PaletteColour, 16> palette = {
        {{"Red", "#FF0000"},
         {"Green", "#008000"},
         {"Blue", "#0000FF"},
         {"Cyan", "#00FFFF"},
         {"Magenta", "#FF00FF"},
         {"Yellow", "#FFFF00"},
         {"Black", "#000000"},
         {"White", "#FFFFFF"},
         {"Orange", "#F8651B"},
         {"Brown", "#8B4513"},
         {"Gray", "#808080"},
         {"Purple", "#800080"},
         {"Pink", "#FFC0CB"},
         {"Silver", "#C0C0C0"},
         {"Natural", "#F5F5DC"},
         {"Gold", "#FFD700"}}
    };
    const std::string hex = normalize_color_rgba(color_hex);
    const auto distance = [&](const PaletteColour &c) {
        int d = 0;
        for (int i = 1; i < 7; i += 2) {
            const int x = std::stoi(hex.substr(i, 2), nullptr, 16) -
                std::stoi(std::string(c.hex + i, 2), nullptr, 16);
            d += x * x;
        }
        return d;
    };

    // The palette colors the product name names, each once, in order
    std::vector<const PaletteColour *> named;
    std::string word;
    for (unsigned char ch : product + ' ') {
        if (std::isalnum(ch)) {
            word += char(ch);
            continue;
        }
        if (word.empty())
            continue;
        if (iequals(word, "grey"))
            word = "Gray";
        for (const PaletteColour &c : palette)
            if (iequals(word, c.name) && std::find(named.begin(), named.end(), &c) == named.end())
                named.push_back(&c);
        word.clear();
    }
    if (named.size() == 1 || (!named.empty() && hex.empty()))
        return named.back()->name;
    if (!named.empty())
        // "PETG Black Silver Glitter": the one closest to the product's color
        return (*std::min_element(
                    named.begin(), named.end(),
                    [&](const PaletteColour *a, const PaletteColour *b) {
                        return distance(*a) < distance(*b);
                    }
                ))
            ->name;
    // No color in the name, or no name (only what the printer reported): the printer's color
    // exactly, as the printer itself would name it
    for (const PaletteColour &c : palette)
        if (hex == c.hex)
            return c.name;
    return {};
}

std::string connect_error_summary(const std::string &body, unsigned status) {
    const json reply = json::parse(body, nullptr, /*allow_exceptions*/ false);
    if (reply.is_object()) {
        if (const auto message = reply.find("message");
            message != reply.end() && message->is_string()) {
            std::string s = message->get<std::string>();
            if (!s.empty()) {
                // Short enough for a message box, cut between UTF-8 characters
                constexpr size_t max_len = 120;
                if (s.size() > max_len) {
                    size_t cut = max_len - 3;
                    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
                        --cut;
                    s = s.substr(0, cut) + "...";
                }
                return s;
            }
        }
    }
    return "HTTP " + std::to_string(status);
}

bool parse_connect_printers_json(
    const std::string &body,
    std::vector<ConnectPrinter> &out,
    std::vector<std::string> &warnings,
    std::string &error
) {
    out.clear();
    warnings.clear();
    try {
        const json root = json::parse(body, nullptr, /*allow_exceptions*/ false);
        if (root.is_discarded()) {
            error = "the reply isn't JSON";
            return false;
        }
        // {"printers":[...]}, or that object alone in an array
        const json *holder = root.is_object() ?
            &root :
            (root.is_array() && root.size() == 1 && root.front().is_object()) ?
            &root.front() :
            nullptr;
        const json *list = nullptr;
        if (holder)
            if (const auto it = holder->find("printers"); it != holder->end() && it->is_array())
                list = &*it;
        if (!list) {
            error = "the reply has no \"printers\" list";
            return false;
        }

        size_t n = 0;
        for (const json &p : *list) {
            ++n;
            if (!p.is_object()) {
                warnings.emplace_back("printer " + std::to_string(n) + ": not an object, skipped");
                continue;
            }
            ConnectPrinter cp;
            cp.uuid = string_at(p, {"uuid"});
            if (cp.uuid.empty()) {
                warnings.emplace_back("printer " + std::to_string(n) + ": no uuid, skipped");
                continue;
            }
            cp.name = string_at(p, {"name"});
            cp.state = string_at(p, {"connect_state"});
            cp.model = string_at(p, {"model"});
            // A string; a list in the vendor profiles, so its first entry is taken if Connect
            // ever sends one
            if (const auto legacy = p.find("legacy_printer_model"); legacy != p.end()) {
                if (legacy->is_string())
                    cp.legacy_model = legacy->get<std::string>();
                else if (legacy->is_array() && !legacy->empty() && legacy->front().is_string())
                    cp.legacy_model = legacy->front().get<std::string>();
            }
            if (const auto supported = p.find("supported_printer_models");
                supported != p.end() && supported->is_array())
                for (const json &m : *supported)
                    if (m.is_string())
                        cp.supported_models.push_back(m.get<std::string>());

            cp.tools.source = "Prusa Connect";
            const auto tools = p.find("tools");
            if (tools != p.end() && tools->is_object())
                parse_connect_tools(*tools, cp.tools, cp.has_feeder_slots);
            else if (tools != p.end() && !tools->is_null())
                cp.tools.warnings.emplace_back("\"tools\" isn't an object, ignored");
            out.push_back(std::move(cp));
        }
    } catch (const json::exception &e) {
        out.clear();
        error = e.what();
        return false;
    }
    return true;
}

} // namespace printer_tools_detail
} // namespace Slic3r
