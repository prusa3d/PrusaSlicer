///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer Calibration Tools
///|/
#include "FilamentDB.hpp"
#include "Http.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <boost/log/trivial.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/filesystem.hpp>
#include <sstream>
#include <unordered_map>

namespace Slic3r {

std::vector<FilamentDBPreset> parse_filamentdb_bundle(const std::string &ini_content)
{
    std::vector<FilamentDBPreset> presets;
    std::istringstream stream(ini_content);
    std::string line;

    FilamentDBPreset *current = nullptr;

    while (std::getline(stream, line)) {
        boost::algorithm::trim(line);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#')
            continue;

        // Check for section header [filament:Name]
        if (line.front() == '[' && line.back() == ']') {
            std::string section = line.substr(1, line.size() - 2);
            const std::string prefix = "filament:";

            if (section.compare(0, prefix.size(), prefix) == 0) {
                std::string name = section.substr(prefix.size());
                boost::algorithm::trim(name);

                // Skip internal/abstract presets (PrusaSlicer convention)
                if (!name.empty() && name.front() == '*' && name.back() == '*') {
                    current = nullptr;
                    continue;
                }

                presets.emplace_back();
                current = &presets.back();
                current->name = name;
            } else {
                // Non-filament section — stop tracking
                current = nullptr;
            }
            continue;
        }

        // Parse key = value pairs
        if (current) {
            auto eq_pos = line.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = line.substr(0, eq_pos);
                std::string value = line.substr(eq_pos + 1);
                boost::algorithm::trim(key);
                boost::algorithm::trim(value);

                // Extract vendor and type for metadata
                if (key == "filament_vendor")
                    current->vendor = value;
                else if (key == "filament_type")
                    current->type = value;

                current->config_pairs.emplace_back(std::move(key), std::move(value));
            }
        }
    }

    return presets;
}

int load_filaments_from_filamentdb(
    PresetBundle &preset_bundle,
    const std::string &api_url,
    std::string &error_message)
{
    // Build the endpoint URL
    std::string url = api_url;
    // Ensure trailing slash
    if (!url.empty() && url.back() != '/')
        url += '/';
    url += "api/filaments/prusaslicer";

    BOOST_LOG_TRIVIAL(info) << "FilamentDB: Fetching presets from " << url;

    std::string response_body;
    bool success = false;

    auto http = Http::get(std::move(url));

    http.on_error([&](std::string body, std::string error, unsigned status) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentDB: HTTP error: " << error
                                   << " (status " << status << ")";
        error_message = "FilamentDB connection failed: " + error;
        if (status > 0)
            error_message += " (HTTP " + std::to_string(status) + ")";
    })
    .on_complete([&](std::string body, unsigned status) {
        if (status != 200) {
            error_message = "FilamentDB returned HTTP " + std::to_string(status);
            BOOST_LOG_TRIVIAL(warning) << error_message;
            return;
        }
        response_body = std::move(body);
        success = true;
    })
    .perform_sync();

    if (!success)
        return -1;

    // Parse the INI bundle
    auto remote_presets = parse_filamentdb_bundle(response_body);

    if (remote_presets.empty()) {
        BOOST_LOG_TRIVIAL(info) << "FilamentDB: No filament presets found in response";
        return 0;
    }

    BOOST_LOG_TRIVIAL(info) << "FilamentDB: Parsed " << remote_presets.size() << " presets";

    int loaded = 0;

    for (const auto &rp : remote_presets) {
        try {
            // Start with default filament config
            DynamicPrintConfig config;
            config.apply(FullPrintConfig::defaults());

            // Apply all key-value pairs from the remote preset
            ConfigSubstitutionContext substitution_ctx(ForwardCompatibilitySubstitutionRule::Enable);
            for (const auto &[key, value] : rp.config_pairs) {
                try {
                    config.set_deserialize(key, value, substitution_ctx);
                } catch (const std::exception &) {
                    // Skip unknown or invalid keys — the remote may have
                    // keys from a newer PrusaSlicer version
                    BOOST_LOG_TRIVIAL(trace) << "FilamentDB: Skipping unknown key '"
                                             << key << "' for preset '" << rp.name << "'";
                }
            }

            // Normalize the config (e.g. set extruder to 0 for single-extruder)
            Preset::normalize(config);

            // Load the preset into the filament collection.
            // Pass config by const-ref so the overload that merges with
            // default_preset().config is used — this ensures all required
            // keys exist and prevents null-dereference in the filament tab UI.
            preset_bundle.filaments.load_preset(
                "",
                rp.name,
                config,
                false  // don't select
            );

            BOOST_LOG_TRIVIAL(debug) << "FilamentDB: Loaded preset '" << rp.name << "'";
            ++loaded;

        } catch (const std::exception &ex) {
            BOOST_LOG_TRIVIAL(warning) << "FilamentDB: Failed to load preset '"
                                       << rp.name << "': " << ex.what();
        }
    }

    BOOST_LOG_TRIVIAL(info) << "FilamentDB: Successfully loaded " << loaded << " presets";
    return loaded;
}

FilamentCalibration fetch_filament_calibration(
    const std::string &api_url,
    const std::string &filament_name,
    double nozzle_diameter)
{
    FilamentCalibration result;

    std::string url = api_url;
    if (!url.empty() && url.back() != '/')
        url += '/';
    url += "api/filaments/" + Http::url_encode(filament_name)
         + "/calibration?nozzle_diameter=" + std::to_string(nozzle_diameter);

    BOOST_LOG_TRIVIAL(info) << "FilamentDB: Fetching calibration for '"
                            << filament_name << "' at " << nozzle_diameter << "mm";

    std::string response_body;
    auto http = Http::get(std::move(url));
    http.on_complete([&](std::string body, unsigned status) {
            if (status == 200) {
                response_body = std::move(body);
                result.found = true;
            } else {
                BOOST_LOG_TRIVIAL(debug) << "FilamentDB: No calibration (HTTP " << status << ")";
            }
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(debug) << "FilamentDB: Calibration fetch failed: " << error;
        })
        .perform_sync();

    if (!result.found || response_body.empty())
        return result;

    // Simple JSON value extraction (avoids adding a JSON library dependency)
    auto extract_double = [&](const std::string &key) -> double {
        std::string search = "\"" + key + "\":";
        auto pos = response_body.find(search);
        if (pos == std::string::npos) return -1;
        pos += search.size();
        // Skip whitespace
        while (pos < response_body.size() && (response_body[pos] == ' ' || response_body[pos] == '\t'))
            ++pos;
        if (pos >= response_body.size() || response_body.substr(pos, 4) == "null")
            return -1;
        try { return std::stod(response_body.substr(pos)); }
        catch (...) { return -1; }
    };

    result.pressure_advance    = extract_double("pressureAdvance");
    result.max_volumetric_speed = extract_double("maxVolumetricSpeed");
    result.extrusion_multiplier = extract_double("extrusionMultiplier");
    result.retract_length      = extract_double("retractLength");
    result.retract_speed       = extract_double("retractSpeed");
    result.retract_lift        = extract_double("retractLift");

    BOOST_LOG_TRIVIAL(info) << "FilamentDB: Calibration for '" << filament_name
                            << "' @ " << nozzle_diameter << "mm:"
                            << " PA=" << result.pressure_advance
                            << " MVS=" << result.max_volumetric_speed
                            << " EM=" << result.extrusion_multiplier;
    return result;
}

// Escape a string for JSON (handles quotes, backslashes, control chars)
static std::string json_escape(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += c;      break;
        }
    }
    return out;
}

// Internal helper: do a single HTTP request and capture the status/body/error.
struct HttpAttempt {
    int         status = 0;
    std::string body;
    std::string transport_error; // non-empty if the request didn't even complete
    bool        ok() const { return status >= 200 && status < 300; }
};

static HttpAttempt http_post_json(const std::string &url, const std::string &json)
{
    HttpAttempt r;
    auto http = Http::post(url);
    http.header("Content-Type", "application/json")
        .set_post_body(json)
        .on_complete([&](std::string body, unsigned status) {
            r.status = static_cast<int>(status);
            r.body = std::move(body);
        })
        .on_error([&](std::string body, std::string err, unsigned status) {
            r.status = static_cast<int>(status);
            r.body = std::move(body);
            r.transport_error = std::move(err);
        })
        .perform_sync();
    BOOST_LOG_TRIVIAL(debug) << "FilamentDB POST " << url
                             << " -> " << r.status
                             << (r.transport_error.empty() ? "" : (" transport=" + r.transport_error));
    return r;
}

// Extract a top-level JSON string value for `key` from `body`. Hand-rolled (the
// same no-dependency approach as extract_double above) — handles \" / \\ / \n /
// \t / \r escapes and stops at the first unescaped quote. Returns "" if the key
// is absent or its value is null / non-string.
std::string filamentdb_detail::extract_json_string(const std::string &body, const std::string &key)
{
    auto is_ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    // Match the key as a quoted token, then require a ':' with optional whitespace
    // on EITHER side — robust to pretty-printed JSON ("key" : "v" / "key":\n"v").
    // Including both quotes in the needle also guards against a suffix false-match
    // (searching "name" must not hit inside "matchedName").
    const std::string needle = "\"" + key + "\"";
    size_t pos = 0;
    for (;;) {
        pos = body.find(needle, pos);
        if (pos == std::string::npos) return {};
        size_t p = pos + needle.size();
        while (p < body.size() && is_ws(body[p])) ++p;
        if (p < body.size() && body[p] == ':') { pos = p + 1; break; }
        // The token matched but isn't a key here (e.g. it's a string value equal
        // to the key) — keep scanning.
        pos += needle.size();
    }
    while (pos < body.size() && is_ws(body[pos])) ++pos;
    if (pos >= body.size() || body[pos] != '"') return {}; // null / number / missing value
    ++pos; // step past the opening quote
    std::string out;
    while (pos < body.size()) {
        const char c = body[pos++];
        if (c == '\\' && pos < body.size()) {
            const char e = body[pos++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            default:  out += e;    break; // covers \" \\ \/ and anything else literally
            }
        } else if (c == '"') {
            break;
        } else {
            out += c;
        }
    }
    return out;
}
// Bring the detail-namespace helper into Slic3r scope so the file-local helpers
// below (and the sync functions) can call it unqualified. It's declared in the
// header (filamentdb_detail namespace) for unit testing.
using filamentdb_detail::extract_json_string;

// Build the {"name":...,"config":{...}} sync body from a preset's config. Every
// key in config.keys() is serialized — including the registered filamentdb_id, so
// the stable id rides along automatically with no special-casing here.
static std::string build_sync_body(const std::string &name, const DynamicPrintConfig &config)
{
    std::string body = "{\"name\":\"" + json_escape(name) + "\",\"config\":{";
    bool first = true;
    for (const std::string &key : config.keys()) {
        if (!first) body += ',';
        body += "\"" + json_escape(key) + "\":\"" + json_escape(config.opt_serialize(key)) + "\"";
        first = false;
    }
    body += "}}";
    return body;
}

// Minimal id-addressed body for a PURE RENAME: only the target name plus the
// filamentdb_id (present solely to satisfy the server's non-empty-config guard).
// None of the mapped calibration/structured keys are included, so the server
// rewrites only the record's name and leaves its saved settings/calibration
// intact (#36 Phase 2 "rename the DB record only").
static std::string build_rename_body(const std::string &name, const std::string &filament_id)
{
    return "{\"name\":\"" + json_escape(name) + "\",\"config\":{\"filamentdb_id\":\""
         + json_escape(filament_id) + "\"}}";
}

// Populate matched_by/matched_name/matched_id on a SUCCESS (2xx) result from the
// server's response body (#867: matchedBy/matchedName/filamentId). Lets the GUI
// re-stamp a name-matched preset with the resolved id.
static void parse_match_fields(FilamentDBSyncResult &result, const std::string &body)
{
    result.matched_by   = extract_json_string(body, "matchedBy");
    result.matched_name = extract_json_string(body, "matchedName");
    if (result.matched_id.empty())
        result.matched_id = extract_json_string(body, "filamentId");
}

namespace filamentdb_detail {

std::string config_first_string(const DynamicPrintConfig &cfg, const char *key)
{
    const ConfigOption *opt = cfg.option(key);
    if (opt == nullptr)
        return {};
    if (auto *s = dynamic_cast<const ConfigOptionString *>(opt))
        return s->value;
    if (auto *ss = dynamic_cast<const ConfigOptionStrings *>(opt))
        return ss->values.empty() ? std::string() : ss->values.front();
    return {};
}

std::vector<FilamentDBCandidate> bundle_to_candidates(const std::string &ini_content)
{
    // A multi-nozzle filament exports as several sections ("<base> <Ø type>") that
    // all carry the same filamentdb_id. De-dupe by id and keep the SHORTEST name
    // (the base — the per-nozzle suffix only ever lengthens it) as the display
    // label. Sections without a filamentdb_id are skipped (can't link without a
    // stable id). Insertion order (bundle order, i.e. name-sorted server-side) is
    // preserved for the first occurrence of each id.
    std::vector<FilamentDBCandidate> out;
    std::unordered_map<std::string, size_t> id_index;
    for (const FilamentDBPreset &p : parse_filamentdb_bundle(ini_content)) {
        std::string id;
        for (const auto &kv : p.config_pairs)
            if (kv.first == "filamentdb_id") { id = kv.second; break; }
        if (id.empty())
            continue;

        auto it = id_index.find(id);
        if (it == id_index.end()) {
            id_index.emplace(id, out.size());
            out.push_back(FilamentDBCandidate{ id, p.name, p.vendor, p.type });
        } else {
            // Same filament across per-nozzle sections: keep the shortest name (the
            // base), and backfill vendor/type from any section that carries them (a
            // per-nozzle variant may inherit and omit them).
            FilamentDBCandidate &c = out[it->second];
            if (p.name.size() < c.name.size()) c.name = p.name;
            if (c.vendor.empty() && !p.vendor.empty()) c.vendor = p.vendor;
            if (c.type.empty()   && !p.type.empty())   c.type   = p.type;
        }
    }
    return out;
}

} // namespace filamentdb_detail

FilamentDBSyncResult sync_filament_to_filamentdb_detailed(
    const std::string &api_url,
    const std::string &preset_name,
    const DynamicPrintConfig &config,
    double nozzle_diameter,
    bool high_flow,
    bool allow_create)
{
    FilamentDBSyncResult result;

    if (api_url.empty()) {
        result.error_message = "FilamentDB URL is not configured (Preferences → filamentdb_url)";
        return result;
    }

    // Build base URL and the per-name endpoint with nozzle query params.
    std::string base = api_url;
    if (!base.empty() && base.back() == '/')
        base.pop_back();
    const std::string name_url_unqueried = base + "/api/filaments/" + Http::url_encode(preset_name);
    const std::string query = (nozzle_diameter > 0)
        ? "?nozzle_diameter=" + std::to_string(nozzle_diameter)
            + "&high_flow=" + (high_flow ? "1" : "0")
        : std::string();
    const std::string name_url = name_url_unqueried + query;

    // Build update body: {"name": "...", "config": {...}}. The registered
    // filamentdb_id rides along automatically (build_sync_body emits every key).
    const std::string update_body = build_sync_body(preset_name, config);

    BOOST_LOG_TRIVIAL(info) << "FilamentDB: Syncing preset '" << preset_name << "' (POST " << name_url << ")";

    // Attempt 1: POST /api/filaments/{name} — canonical update path.
    HttpAttempt a1 = http_post_json(name_url, update_body);
    result.method = "POST";
    result.http_status = a1.status;

    if (a1.ok()) {
        // #36 Phase 2: a 201 means the per-name POST CREATED the record — some
        // servers implement it as a create-on-missing upsert. With create suppressed
        // (allow_create=false, the manual toolbar sync), that would silently create
        // and bypass the Create/Link prompt, so route it through would_create like
        // any other create-fallback. A 200 (updated an existing record) is a normal
        // sync and reports success.
        if (!allow_create && a1.status == 201) {
            result.would_create = true;
            parse_match_fields(result, a1.body); // bind the new id if the GUI proceeds
            BOOST_LOG_TRIVIAL(info) << "FilamentDB: '" << preset_name
                                    << "' created by upsert (201); create suppressed";
            return result;
        }
        result.success = true;
        result.created_new = (a1.status == 201);
        result.existed_before = !result.created_new;
        parse_match_fields(result, a1.body); // #36: matchedBy/matchedName/filamentId for GUI re-stamp
        BOOST_LOG_TRIVIAL(info) << "FilamentDB: Synced '" << preset_name << "' (HTTP " << a1.status << ")";
        return result;
    }

    // #36 Phase 2: a 409 name_id_mismatch is a RECONCILE signal, not a transport
    // failure — the server refused to mutate because the carried filamentdb_id
    // resolves to a differently-named filament (a renamed preset vs a copied id,
    // indistinguishable server-side). Surface the structured fields so the GUI can
    // prompt the user, instead of folding the raw body into a generic error below.
    if (a1.status == 409 && extract_json_string(a1.body, "error") == "name_id_mismatch") {
        result.http_status      = 409;
        result.name_id_mismatch = true;
        result.matched_by       = "id";
        result.matched_id       = extract_json_string(a1.body, "filamentId");
        result.matched_name     = extract_json_string(a1.body, "matchedName");
        result.sent_name        = extract_json_string(a1.body, "sentName");
        result.error_message    = "name_id_mismatch";
        BOOST_LOG_TRIVIAL(info) << "FilamentDB: 409 name_id_mismatch for '" << preset_name
                                << "' — filamentdb_id resolves to '" << result.matched_name << "'";
        return result; // success stays false; the GUI branches on name_id_mismatch
    }

    // 404 from the per-name route means "filament not found" → switch to create.
    // Other failures (500, network errors) are reported as-is without retry.
    if (a1.status != 404 && a1.status != 405 && a1.status != 501) {
        if (!a1.transport_error.empty())
            result.error_message = a1.transport_error;
        else
            result.error_message = "HTTP " + std::to_string(a1.status)
                                 + (a1.body.empty() ? "" : (" — " + a1.body.substr(0, 200)));
        BOOST_LOG_TRIVIAL(warning) << result.error_message;
        return result;
    }

    // #36 Phase 2: the per-name POST hit a status the upsert treats as a
    // create-fallback — 404 (no such record) or 405/501 (no per-name route, e.g. a
    // server that only supports collection create). With create suppressed
    // (allow_create=false — the manual toolbar sync), report `would_create` on
    // EXACTLY the cases the auto path would have collection-created on, so the GUI
    // offers Create-new / Link-to-existing instead of failing outright. It's a
    // decision point, not an error — leave error_message empty. (If create is then
    // attempted and genuinely fails, that surfaces as an error from Attempt 2/3.)
    if (!allow_create) {
        result.would_create  = true;
        result.http_status   = a1.status; // 404/405/501
        result.error_message.clear();
        BOOST_LOG_TRIVIAL(info) << "FilamentDB: '" << preset_name
                                << "' has no updatable record (HTTP " << a1.status
                                << "); create suppressed (allow_create=false)";
        return result;
    }

    // Attempt 2: collection create — POST /api/filaments with {name, type, vendor, config}.
    // Server requires `type` and `vendor`; everything else is optional and stored
    // server-side (see Filament model). We extract from the preset's config.
    std::string filament_type   = filamentdb_detail::config_first_string(config, "filament_type");
    std::string filament_vendor = filamentdb_detail::config_first_string(config, "filament_vendor");
    if (filament_type.empty())   filament_type   = "PLA";
    if (filament_vendor.empty()) filament_vendor = "User";

    std::string create_body = "{"
        "\"name\":\""   + json_escape(preset_name)    + "\","
        "\"type\":\""   + json_escape(filament_type)  + "\","
        "\"vendor\":\"" + json_escape(filament_vendor) + "\""
        "}";

    const std::string collection_url = base + "/api/filaments";
    BOOST_LOG_TRIVIAL(info) << "FilamentDB: '" << preset_name << "' missing — creating via POST " << collection_url;

    HttpAttempt a2 = http_post_json(collection_url, create_body);
    if (!a2.ok()) {
        result.method = "POST collection";
        result.http_status = a2.status;
        if (!a2.transport_error.empty())
            result.error_message = "Create failed: " + a2.transport_error;
        else
            result.error_message = "Create failed: HTTP " + std::to_string(a2.status)
                                 + (a2.body.empty() ? "" : (" — " + a2.body.substr(0, 200)));
        BOOST_LOG_TRIVIAL(warning) << result.error_message;
        return result;
    }

    // Attempt 3: now that the entry exists, retry the per-name update so the
    // server populates calibrations / per-nozzle data. This is the "second
    // half" of the upsert: create gave us metadata, update gives us the
    // calibration body that the create endpoint silently ignored.
    BOOST_LOG_TRIVIAL(info) << "FilamentDB: shell created (HTTP " << a2.status << "); retrying update";
    HttpAttempt a3 = http_post_json(name_url, update_body);
    result.method = "POST (after create)";
    result.http_status = a3.status;
    if (!a3.ok()) {
        // Create did succeed — the calibration push didn't. Treat as soft-success
        // but surface the partial state.
        result.success = true;
        result.created_new = true;
        result.existed_before = false;
        result.error_message = "Created '" + preset_name + "' but calibration update returned HTTP "
                             + std::to_string(a3.status);
        BOOST_LOG_TRIVIAL(warning) << result.error_message;
        return result;
    }

    result.success = true;
    result.created_new = true;
    result.existed_before = false;
    parse_match_fields(result, a3.body); // #36: bind the freshly-created record's id too
    BOOST_LOG_TRIVIAL(info) << "FilamentDB: Created and populated '" << preset_name
                            << "' (create=" << a2.status << ", update=" << a3.status << ")";
    return result;
}

bool sync_filament_to_filamentdb(
    const std::string &api_url,
    const std::string &preset_name,
    const DynamicPrintConfig &config,
    std::string &error_message,
    double nozzle_diameter,
    bool high_flow)
{
    auto r = sync_filament_to_filamentdb_detailed(api_url, preset_name, config,
                                                   nozzle_diameter, high_flow);
    error_message = r.error_message;
    return r.success;
}

FilamentDBSyncResult resync_filament_to_filamentdb_by_id(
    const std::string &api_url,
    const std::string &filament_id,
    const std::string &preset_name,
    const DynamicPrintConfig &config,
    double nozzle_diameter,
    bool high_flow,
    bool rename_only)
{
    FilamentDBSyncResult result;

    if (api_url.empty()) {
        result.error_message = "FilamentDB URL is not configured (Preferences → filamentdb_url)";
        return result;
    }
    if (filament_id.empty()) {
        result.error_message = "No filament id to re-sync";
        return result;
    }

    std::string base = api_url;
    if (!base.empty() && base.back() == '/')
        base.pop_back();
    // A pure rename pushes no calibration, so the nozzle query (which only routes
    // per-nozzle calibration server-side) is omitted along with the config body.
    const std::string query = (!rename_only && nozzle_diameter > 0)
        ? "?nozzle_diameter=" + std::to_string(nozzle_diameter)
            + "&high_flow=" + (high_flow ? "1" : "0")
        : std::string();
    // ObjectId addressing — the AUTHORITATIVE form. The server (#867) resolves by
    // this URL id and ignores any filamentdb_id the config body still carries, so a
    // stale/copied id can't redirect the write. No create-on-404 fallback: a 404
    // here means the record was deleted, which is a hard error, not a create.
    const std::string id_url = base + "/api/filaments/" + Http::url_encode(filament_id) + query;
    // Carry the LOCAL preset name in the body so the authoritative (id-addressed)
    // update can propagate a rename to the DB record. Without it, a locally-renamed
    // preset would keep the DB record under its old name and hit name_id_mismatch
    // again on every later sync (Codex P2). rename_only sends the name alone (no
    // calibration/structured keys) so the record's saved settings are preserved.
    const std::string body = rename_only
        ? build_rename_body(preset_name, filament_id)
        : build_sync_body(preset_name, config);

    BOOST_LOG_TRIVIAL(info) << "FilamentDB: re-syncing by id (POST " << id_url
                            << (rename_only ? ", rename-only" : "") << ")";
    HttpAttempt a = http_post_json(id_url, body);
    result.method      = "POST by-id";
    result.http_status = a.status;

    if (a.ok()) {
        result.success        = true;
        result.existed_before = true;
        result.matched_id     = filament_id;
        parse_match_fields(result, a.body);
        BOOST_LOG_TRIVIAL(info) << "FilamentDB: re-synced by id " << filament_id
                                << " (HTTP " << a.status << ")";
        return result;
    }

    if (!a.transport_error.empty())
        result.error_message = a.transport_error;
    else
        result.error_message = "HTTP " + std::to_string(a.status)
                             + (a.body.empty() ? "" : (" — " + a.body.substr(0, 200)));
    BOOST_LOG_TRIVIAL(warning) << "FilamentDB: by-id re-sync failed: " << result.error_message;
    return result;
}

bool list_filamentdb_candidates(
    const std::string &api_url,
    const std::string &vendor,
    const std::string &type,
    std::vector<FilamentDBCandidate> &out,
    std::string &error_message)
{
    out.clear();
    error_message.clear();

    if (api_url.empty()) {
        error_message = "FilamentDB URL is not configured (Preferences → filamentdb_url)";
        return false;
    }

    // Reuse the export bundle (GET /api/filaments/prusaslicer) — it emits
    // filamentdb_id per [filament:Name] section and honors server-side vendor/type
    // exact-match filters, so we get a small, parse-ready candidate set with no
    // JSON dependency (parse_filamentdb_bundle handles the INI).
    std::string url = api_url;
    if (!url.empty() && url.back() != '/')
        url += '/';
    url += "api/filaments/prusaslicer";
    std::string sep = "?";
    if (!vendor.empty()) { url += sep + "vendor=" + Http::url_encode(vendor); sep = "&"; }
    if (!type.empty())   { url += sep + "type="   + Http::url_encode(type);   sep = "&"; }

    BOOST_LOG_TRIVIAL(info) << "FilamentDB: listing link candidates (GET " << url << ")";

    std::string response_body;
    bool ok = false;
    auto http = Http::get(std::move(url));
    http.on_complete([&](std::string body, unsigned status) {
            if (status == 200) {
                response_body = std::move(body);
                ok = true;
            } else {
                error_message = "FilamentDB returned HTTP " + std::to_string(status);
            }
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            error_message = "FilamentDB connection failed: " + error;
            if (status > 0)
                error_message += " (HTTP " + std::to_string(status) + ")";
        })
        .perform_sync();

    if (!ok) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentDB: candidate list failed: " << error_message;
        return false;
    }

    out = filamentdb_detail::bundle_to_candidates(response_body);
    BOOST_LOG_TRIVIAL(info) << "FilamentDB: " << out.size() << " link candidate(s)";
    return true;
}

SpoolCheckResult check_filament_spool(
    const std::string &api_url,
    const std::string &filament_name,
    double weight_g)
{
    SpoolCheckResult result;
    result.filament_name = filament_name;
    result.required_weight_g = weight_g;

    if (weight_g <= 0 || filament_name.empty() || api_url.empty())
        return result; // ok=true, has_data=false

    std::string url = api_url;
    if (!url.empty() && url.back() != '/')
        url += '/';
    url += "api/filaments/" + Http::url_encode(filament_name)
         + "/spool-check?weight=" + std::to_string(weight_g);

    BOOST_LOG_TRIVIAL(info) << "FilamentDB: Spool check for '" << filament_name
                            << "' requiring " << weight_g << "g";

    std::string response_body;
    auto http = Http::get(std::move(url));
    http.on_complete([&](std::string body, unsigned status) {
            if (status == 200) {
                response_body = std::move(body);
            } else {
                BOOST_LOG_TRIVIAL(debug) << "FilamentDB: Spool check returned HTTP " << status;
            }
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(debug) << "FilamentDB: Spool check failed: " << error;
        })
        .perform_sync();

    if (response_body.empty())
        return result; // ok=true, has_data=false — can't reach server, don't block

    // Simple JSON parsing (no library dependency)
    // Check "ok":true/false
    auto pos_ok = response_body.find("\"ok\":");
    if (pos_ok != std::string::npos) {
        auto val_start = pos_ok + 5;
        while (val_start < response_body.size() && response_body[val_start] == ' ')
            ++val_start;
        result.ok = response_body.substr(val_start, 4) == "true";
        result.has_data = true;
    }

    // Extract "warning":"..." — scan for the closing quote while honoring
    // JSON escapes so an embedded \" (e.g. a quoted spool label inside the
    // message) doesn't truncate the text. Decode the common escapes inline
    // so the user-facing string renders cleanly.
    auto pos_warn = response_body.find("\"warning\":\"");
    if (pos_warn != std::string::npos) {
        auto parse_hex4 = [](const std::string &s, std::size_t at, std::uint32_t &out) {
            if (at + 4 > s.size()) return false;
            std::uint32_t v = 0;
            for (int k = 0; k < 4; ++k) {
                char ch = s[at + k];
                int d;
                if (ch >= '0' && ch <= '9') d = ch - '0';
                else if (ch >= 'a' && ch <= 'f') d = 10 + (ch - 'a');
                else if (ch >= 'A' && ch <= 'F') d = 10 + (ch - 'A');
                else return false;
                v = (v << 4) | static_cast<std::uint32_t>(d);
            }
            out = v;
            return true;
        };
        auto append_utf8 = [](std::string &dst, std::uint32_t cp) {
            if (cp < 0x80) {
                dst += static_cast<char>(cp);
            } else if (cp < 0x800) {
                dst += static_cast<char>(0xC0 |  (cp >> 6));
                dst += static_cast<char>(0x80 |  (cp        & 0x3F));
            } else if (cp < 0x10000) {
                dst += static_cast<char>(0xE0 |  (cp >> 12));
                dst += static_cast<char>(0x80 | ((cp >> 6)  & 0x3F));
                dst += static_cast<char>(0x80 |  (cp        & 0x3F));
            } else {
                dst += static_cast<char>(0xF0 |  (cp >> 18));
                dst += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                dst += static_cast<char>(0x80 | ((cp >> 6)  & 0x3F));
                dst += static_cast<char>(0x80 |  (cp        & 0x3F));
            }
        };
        std::size_t i = pos_warn + 11;
        std::string decoded;
        decoded.reserve(64);
        bool terminated = false;
        while (i < response_body.size()) {
            char c = response_body[i];
            if (c == '"') { terminated = true; break; }
            if (c == '\\' && i + 1 < response_body.size()) {
                char esc = response_body[i + 1];
                switch (esc) {
                    case '"':  decoded += '"';  i += 2; continue;
                    case '\\': decoded += '\\'; i += 2; continue;
                    case '/':  decoded += '/';  i += 2; continue;
                    case 'n':  decoded += '\n'; i += 2; continue;
                    case 't':  decoded += '\t'; i += 2; continue;
                    case 'r':  decoded += '\r'; i += 2; continue;
                    case 'b':  decoded += '\b'; i += 2; continue;
                    case 'f':  decoded += '\f'; i += 2; continue;
                    case 'u': {
                        // \uXXXX — decode to UTF-8. Recognize a UTF-16
                        // surrogate pair (\uD8xx\uDCxx) so non-BMP code
                        // points (emoji, etc.) round-trip correctly. An
                        // unpaired surrogate (lone high or low) would
                        // produce invalid UTF-8, so substitute U+FFFD.
                        std::uint32_t cp = 0;
                        if (!parse_hex4(response_body, i + 2, cp)) {
                            // Malformed — preserve the literal escape so
                            // information isn't silently lost.
                            decoded += c;
                            ++i;
                            continue;
                        }
                        std::size_t consumed = 6;
                        if (cp >= 0xD800 && cp <= 0xDBFF
                            && i + 12 <= response_body.size()
                            && response_body[i + 6] == '\\'
                            && response_body[i + 7] == 'u') {
                            std::uint32_t low = 0;
                            if (parse_hex4(response_body, i + 8, low)
                                && low >= 0xDC00 && low <= 0xDFFF) {
                                cp = 0x10000
                                    + ((cp - 0xD800) << 10)
                                    +  (low - 0xDC00);
                                consumed = 12;
                            }
                        }
                        if (cp >= 0xD800 && cp <= 0xDFFF)
                            cp = 0xFFFD;
                        append_utf8(decoded, cp);
                        i += consumed;
                        continue;
                    }
                    default:
                        // Unknown / non-JSON escape — preserve verbatim
                        // (both the backslash and the following char) so
                        // we don't silently corrupt the message.
                        decoded += c;
                        ++i;
                        continue;
                }
            }
            decoded += c;
            ++i;
        }
        if (terminated)
            result.warning = std::move(decoded);
    }

    // If no spools array or "message" about no data, treat as no data
    if (response_body.find("\"spools\":[]") != std::string::npos)
        result.has_data = false;

    BOOST_LOG_TRIVIAL(info) << "FilamentDB: Spool check result: ok=" << result.ok
                            << " has_data=" << result.has_data
                            << (result.warning.empty() ? "" : " warning=" + result.warning);
    return result;
}

} // namespace Slic3r
