///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer Calibration Tools
///|/
#ifndef slic3r_Utils_FilamentDB_hpp_
#define slic3r_Utils_FilamentDB_hpp_

#include <string>
#include <vector>
#include <functional>

namespace Slic3r {

class DynamicPrintConfig;
class PresetBundle;

// Represents a single filament preset fetched from a Filament DB REST API.
struct FilamentDBPreset {
    std::string name;
    std::string vendor;
    std::string type;
    // All key-value pairs from the INI section, ready to load into DynamicPrintConfig.
    std::vector<std::pair<std::string, std::string>> config_pairs;
};

// Fetch filament presets from a Filament DB REST API and load them into the
// PresetBundle's filament collection.
//
// The API endpoint is expected to return a PrusaSlicer-compatible INI config
// bundle with [filament:Name] sections (GET /api/filaments/prusaslicer).
//
// Presets loaded from the remote are marked as user presets and saved to the
// local filament preset directory so they persist across restarts. A fresh
// fetch replaces the previously synced presets.
//
// Returns the number of presets loaded, or -1 on error.
int load_filaments_from_filamentdb(
    PresetBundle &preset_bundle,
    const std::string &api_url,
    std::string &error_message
);

// Parse a PrusaSlicer INI config bundle string into FilamentDBPreset structs.
// This is a standalone function for testability.
std::vector<FilamentDBPreset> parse_filamentdb_bundle(const std::string &ini_content);

// Calibration data returned from FilamentDB for a specific nozzle diameter.
struct FilamentCalibration {
    double pressure_advance{-1};      // -1 = not set
    double max_volumetric_speed{-1};
    double extrusion_multiplier{-1};
    double retract_length{-1};
    double retract_speed{-1};
    double retract_lift{-1};
    bool   found{false};
};

// Fetch calibration data for a filament at a specific nozzle diameter.
// Queries GET {api_url}/api/filaments/{name}/calibration?nozzle_diameter={diameter}
// Returns a FilamentCalibration struct (found=false if no match or error).
FilamentCalibration fetch_filament_calibration(
    const std::string &api_url,
    const std::string &filament_name,
    double nozzle_diameter
);

// Detailed result of a sync attempt.
struct FilamentDBSyncResult {
    bool        success = false;
    int         http_status = 0;       // last HTTP status seen
    std::string method;                // "POST", "PUT", "POST collection"
    std::string error_message;         // empty on success
    bool        existed_before = false; // true if pre-check GET returned 2xx
    bool        created_new = false;    // best-effort: true if first POST returned 201
                                        // or if pre-check was 404 and a later attempt 2xx'd

    // #36 Phase 2 — reconcile fields parsed from the server response body.
    // The #867 server resolves a name-addressed sync by a carried `filamentdb_id`
    // first; when that id resolves to a DIFFERENTLY-named filament it returns 409
    // `name_id_mismatch` WITHOUT mutating (a rename vs a copied/cloned id are
    // indistinguishable server-side). The GUI uses these to prompt the user.
    bool        name_id_mismatch = false; // true iff the server returned 409 name_id_mismatch
    std::string matched_by;               // "id" | "name" | "" — how the 200 resolved the record
    std::string matched_id;               // the filament _id the server resolved / conflicted on
    std::string matched_name;             // the stored name of that filament
    std::string sent_name;                // the preset name we sent (echoed back on a 409)

    // #36 Phase 2 — no-match interception. Set (with success=false) when a
    // name-addressed sync hit a status the upsert treats as a create-fallback
    // (404 not-found, or 405/501 no per-name route) AND create was suppressed
    // (allow_create=false). It's a decision point, not an error: the GUI prompts
    // Create-new vs Link-to-existing — the same cases the auto path would have
    // collection-created — instead of failing outright. error_message stays empty.
    bool        would_create = false;
};

// Sync a filament preset to the FilamentDB server with upsert semantics.
//
// Strategy (each attempt non-fatal; we fall through on 404/405/501):
//   1. POST /api/filaments/{name}            — canonical upsert
//   2. PUT  /api/filaments/{name}            — alternative upsert verb
//   3. POST /api/filaments  (name in body)   — REST collection-create
//
// When nozzle_diameter > 0 it is appended as ?nozzle_diameter=...&high_flow=0|1
// so the server can update the correct per-nozzle calibration entry.
//
// Also performs a pre-check GET to populate existed_before / created_new so
// the caller can show "Created new filament" vs "Updated filament" notifications.
//
// When allow_create is false, a "not found" (the server has no matching record)
// does NOT auto-create: the result comes back with would_create=true and
// success=false so the caller can prompt the user (Create new / Link to an
// existing filament). Default true preserves the original upsert behavior.
FilamentDBSyncResult sync_filament_to_filamentdb_detailed(
    const std::string &api_url,
    const std::string &preset_name,
    const DynamicPrintConfig &config,
    double nozzle_diameter = 0,
    bool high_flow = false,
    bool allow_create = true
);

// Backwards-compatible wrapper. Uses the detailed function under the hood and
// flattens the result to bool + error_message.
bool sync_filament_to_filamentdb(
    const std::string &api_url,
    const std::string &preset_name,
    const DynamicPrintConfig &config,
    std::string &error_message,
    double nozzle_diameter = 0,
    bool high_flow = false
);

// Re-sync a filament addressing it by its Filament DB ObjectId instead of by name
// (POST /api/filaments/{id}). This is the AUTHORITATIVE form — the server applies
// the update to exactly that record and a carried filamentdb_id can't redirect it.
// Used by the GUI "Update anyway" action after a 409 name_id_mismatch, where the
// user has confirmed the id is the intended target. No create-on-404 fallback: a
// missing id is a hard error (the record was deleted), not a reason to spawn one.
//
// When rename_only is true, the body carries ONLY the target name (plus the id,
// to satisfy the server's non-empty-config guard) — no calibration/structured
// keys and no nozzle query — so the server rewrites just the record's name and
// leaves its saved settings/calibration intact. This backs the reconcile
// "rename the DB record only" choice, distinct from a full push (rename_only=false).
FilamentDBSyncResult resync_filament_to_filamentdb_by_id(
    const std::string &api_url,
    const std::string &filament_id,
    const std::string &preset_name,
    const DynamicPrintConfig &config,
    double nozzle_diameter = 0,
    bool high_flow = false,
    bool rename_only = false
);

// A minimal filament identity from the DB, used to let the user link a preset to
// an existing record (#36 Phase 2 "Link to existing"). Sourced from the INI
// bundle (GET /api/filaments/prusaslicer), which emits filamentdb_id per section.
struct FilamentDBCandidate {
    std::string id;      // Filament DB ObjectId (from the section's filamentdb_id)
    std::string name;    // display name (the base name when per-nozzle sections collapse)
    std::string vendor;
    std::string type;
};

// List existing filaments a preset could be linked to, optionally narrowed to a
// vendor and/or type (server-side exact-match filters). Fetches the INI bundle
// (GET /api/filaments/prusaslicer) and keeps only records that carry a
// filamentdb_id (linking needs a stable id), de-duplicated by id. Returns true on
// a successful fetch (out may be empty); false + error_message on transport/HTTP
// error.
bool list_filamentdb_candidates(
    const std::string &api_url,
    const std::string &vendor,
    const std::string &type,
    std::vector<FilamentDBCandidate> &out,
    std::string &error_message
);

// Result of a spool remaining-filament check.
struct SpoolCheckResult {
    bool   ok{true};            // true if enough filament (or no data)
    bool   has_data{false};     // true if spool weight data was available
    std::string filament_name;
    double required_weight_g{0};
    std::string warning;        // human-readable warning if !ok
    struct SpoolInfo {
        std::string label;
        double remaining_weight_g{0};
        bool enough{true};
    };
    std::vector<SpoolInfo> spools;
};

// Check whether the filament has enough remaining material for a print.
// Queries GET {api_url}/api/filaments/{name}/spool-check?weight={weight_g}
// Returns a SpoolCheckResult (ok=true if enough or no data available).
SpoolCheckResult check_filament_spool(
    const std::string &api_url,
    const std::string &filament_name,
    double weight_g
);

namespace filamentdb_detail {

// Extract a single string from a typed config option, preserving any embedded
// commas/semicolons (e.g. a vendor like "Prusa Research, a.s.").
// Reads typed ConfigOptionString / ConfigOptionStrings pointers rather than
// opt_serialize, which would treat commas as list separators.
//   filament_vendor is coString  -> reads ConfigOptionString::value
//   filament_type   is coStrings -> reads ConfigOptionStrings::values.front()
// Returns "" if the key is missing or the option type is unexpected.
// Exposed for unit testing.
std::string config_first_string(const DynamicPrintConfig &cfg, const char *key);

// Extract a top-level JSON string value for `key` from a response `body`.
// Hand-rolled (no JSON dependency): handles \" \\ \n \t \r escapes and stops at
// the first unescaped quote. Returns "" if the key is absent or its value is
// null / non-string. Used to read the #867 sync response fields
// (matchedBy / matchedName / filamentId / error / sentName). Exposed for testing.
std::string extract_json_string(const std::string &body, const std::string &key);

// Convert a PrusaSlicer INI bundle into link candidates: keep only sections that
// carry a filamentdb_id, de-duplicated by id, keeping the shortest name (the base
// name, since per-nozzle exports only lengthen it). This is the pure core of
// list_filamentdb_candidates, split out so the id/dedupe/name logic is testable
// without a live server.
std::vector<FilamentDBCandidate> bundle_to_candidates(const std::string &ini_content);

} // namespace filamentdb_detail

} // namespace Slic3r

#endif // slic3r_Utils_FilamentDB_hpp_
