///|/ Copyright (c) Filament DB integration
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "FilamentScanClient.hpp"

#include "GUI_App.hpp"
#include "NotificationManager.hpp"
#include "Plater.hpp"
#include "Sidebar.hpp"
#include "Tab.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "nlohmann/json.hpp"

#include <curl/curl.h>
#include <wx/app.h>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace Slic3r { namespace GUI {

namespace filament_scan_detail {

ParsedEvent parse_record(const std::string& record)
{
    ParsedEvent out;
    std::size_t line_start = 0;
    while (line_start <= record.size()) {
        const std::size_t nl = record.find('\n', line_start);
        const std::string line = record.substr(
            line_start,
            nl == std::string::npos ? std::string::npos : nl - line_start);

        if (! line.empty() && line[0] != ':') {
            // SSE field syntax: "field: value" or "field:value". A
            // leading space after the colon is stripped per the spec.
            if (line.rfind("event:", 0) == 0) {
                std::size_t s = 6;
                if (s < line.size() && line[s] == ' ') ++s;
                out.event_type = line.substr(s);
            } else if (line.rfind("data:", 0) == 0) {
                std::size_t s = 5;
                if (s < line.size() && line[s] == ' ') ++s;
                // EventSource spec: append the field value, then a LF
                // after every `data:` field — including empty values.
                // The trailing LF after the last `data:` is stripped
                // below. The "skip LF for the first non-empty value"
                // shortcut would lose leading empty-data lines like
                // `data:\ndata:{json}\n\n`, corrupting otherwise valid
                // multi-line payloads (codex P2 on PR #13).
                out.data += line.substr(s);
                out.data += '\n';
            }
            // `retry:` is ignored — we run our own reconnect loop with
            // exponential backoff (libcurl doesn't honour the spec's
            // retry hint on its own).
        }
        if (nl == std::string::npos) break;
        line_start = nl + 1;
    }
    // Strip the LF appended by the final `data:` line per the
    // EventSource spec ("If the data buffer's last character is a LF,
    // remove the last character from the data buffer").
    if (! out.data.empty() && out.data.back() == '\n')
        out.data.pop_back();
    return out;
}

void SseRecordParser::feed(const char* chunk, std::size_t n, const Emit& emit)
{
    // 1) Normalise line endings as we append. RFC EventSource allows
    //    CR, LF, or CRLF as line terminators; the original LF-only
    //    split silently dropped events from a CRLF publisher (codex
    //    P1 on PR #13). Collapse to LF here so the split below only
    //    has to look for "\n\n". `m_last_was_cr` preserves the CR
    //    state across chunk boundaries so a CRLF straddling two
    //    chunks doesn't produce two LFs.
    m_buffer.reserve(m_buffer.size() + n);
    for (std::size_t i = 0; i < n; ++i) {
        const char c = chunk[i];
        if (c == '\r') {
            m_buffer.push_back('\n');
            m_last_was_cr = true;
        } else if (c == '\n') {
            if (! m_last_was_cr) m_buffer.push_back('\n');
            // else: CR already emitted as LF; suppress this LF so CRLF
            // is normalised to a single LF.
            m_last_was_cr = false;
        } else {
            m_buffer.push_back(c);
            m_last_was_cr = false;
        }
    }

    // 2) Drain complete records (blank-line terminated). Only emit
    //    when the record produced a non-empty `data` payload — that
    //    skips heartbeat comments (`: hb\n\n`) and any future
    //    metadata-only records.
    for (;;) {
        const std::size_t pos = m_buffer.find("\n\n");
        if (pos == std::string::npos) break;
        const std::string record = m_buffer.substr(0, pos);
        m_buffer.erase(0, pos + 2);
        if (record.empty()) continue;
        ParsedEvent ev = parse_record(record);
        if (! ev.data.empty()) emit(ev);
    }
}

} // namespace filament_scan_detail

namespace {

// Ignore `replay` events older than this — protects against the slicer
// opening hours after a scan and silently flipping the preset to a stale
// filament. Live `scan` events from the publisher are never filtered.
constexpr int64_t REPLAY_MAX_AGE_MS = 10 * 60 * 1000;

// Per-connection parser state carried through libcurl's write callback.
struct StreamState {
    FilamentScanClient*                       self = nullptr;
    filament_scan_detail::SseRecordParser     parser;
};

extern "C" std::size_t scan_stream_write_cb(char* ptr, std::size_t size, std::size_t nmemb, void* userdata)
{
    auto* st = static_cast<StreamState*>(userdata);
    const std::size_t n = size * nmemb;
    st->parser.feed(ptr, n, [st](const filament_scan_detail::ParsedEvent& ev) {
        // SseRecordParser already filters records with empty data (e.g.
        // `: hb` heartbeats), so we can dispatch unconditionally here.
        st->self->handle_event(ev.event_type, ev.data);
    });
    return n;
}

extern "C" int scan_stream_xferinfo_cb(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    // Non-zero return aborts the in-flight transfer with
    // CURLE_ABORTED_BY_CALLBACK so stop() can break us out of a
    // blocking curl_easy_perform without waiting for the next byte.
    auto* stop_flag = static_cast<std::atomic<bool>*>(clientp);
    return (stop_flag != nullptr && stop_flag->load()) ? 1 : 0;
}

} // namespace

FilamentScanClient::FilamentScanClient(std::string base_url)
    : m_base_url(std::move(base_url))
{
    // Strip trailing slashes so endpoint concatenation produces a
    // clean URL regardless of how the user formatted `filamentdb_url`
    // in app config — `http://host:3456/` would otherwise yield
    // `http://host:3456//api/scan/stream?replay=0` and miss strict
    // route matches in some setups (codex P2 on PR #13). Matches the
    // normalization other FilamentDB helpers in this codebase do.
    while (! m_base_url.empty() && m_base_url.back() == '/')
        m_base_url.pop_back();
}

FilamentScanClient::~FilamentScanClient()
{
    stop();
}

void FilamentScanClient::start()
{
    // exchange returns the previous value — bail if we were already
    // running. Calls are idempotent.
    if (m_running.exchange(true)) return;
    m_stop.store(false);
    m_thread = std::thread([this] { this->run(); });
}

void FilamentScanClient::stop()
{
    if (! m_running.exchange(false)) return;
    {
        std::lock_guard<std::mutex> lk(m_stop_mtx);
        m_stop.store(true);
    }
    // Wake the worker if it's currently inside the reconnect-loop's
    // wait_for, so stop() doesn't block for up to 30 s waiting on the
    // next backoff tick.
    m_stop_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void FilamentScanClient::handle_event(const std::string& event_type, const std::string& data_json)
{
    // The server emits two relevant event types — `scan` (live tag read)
    // and `replay` (the most recent scan, sent once on connect).
    // Anything else (heartbeat comments, future event types) is ignored.
    if (event_type != "scan" && event_type != "replay") return;

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(data_json);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "[FilamentDB] scan stream: JSON parse failed: " << e.what();
        return;
    }

    if (! j.contains("filament") || j["filament"].is_null()) {
        // No DB match — the user scanned a tag we don't have a preset
        // for. Leaving the current preset alone is the right behaviour;
        // surfacing this in the UI is a follow-up.
        return;
    }

    // Tolerate forward-incompatible / malformed events: if `filament`
    // is anything other than an object (string, number, array, ...),
    // `value()` would throw nlohmann::json::type_error and the
    // exception would propagate through the libcurl write callback,
    // potentially terminating the scan worker (codex P1 on PR #13).
    const auto& fil = j["filament"];
    if (! fil.is_object()) {
        BOOST_LOG_TRIVIAL(warning)
            << "[FilamentDB] scan event 'filament' is not an object (type="
            << fil.type_name() << "); skipping";
        return;
    }

    std::string preset_name;
    try {
        preset_name = fil.value("name", std::string{});
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "[FilamentDB] scan event filament.name lookup failed: " << e.what();
        return;
    }
    if (preset_name.empty()) return;

    if (event_type == "replay") {
        // Tolerate forward-incompatible timestamp types (e.g. the
        // server starts emitting an ISO 8601 string). Same defensive
        // pattern as the filament.name lookup above — codex P1 on
        // PR #13. Treat any parse failure as "no timestamp" → the
        // stale-replay filter below short-circuits and the event
        // is processed normally rather than crashing the worker.
        int64_t ts = 0;
        try {
            ts = j.value("timestamp", int64_t{0});
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(warning)
                << "[FilamentDB] replay event 'timestamp' not parseable as int64 ("
                << e.what() << "); skipping staleness check";
            ts = 0;
        }
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
        if (ts > 0 && (now_ms - ts) > REPLAY_MAX_AGE_MS) {
            BOOST_LOG_TRIVIAL(info)
                << "[FilamentDB] ignoring stale replay (" << (now_ms - ts) / 1000
                << "s old) for preset '" << preset_name << "'";
            return;
        }
    }

    BOOST_LOG_TRIVIAL(info)
        << "[FilamentDB] received " << event_type << " event for preset '" << preset_name << "'";

    // wx's CallAfter is documented thread-safe — queues the lambda onto
    // the main event loop. Tab::select_preset touches wx widgets and
    // must not be reentered from a worker thread.
    wxGetApp().CallAfter([preset_name]() {
        auto& app = wxGetApp();
        // Defensive shutdown / GUI-recreate guards. Two windows where
        // a worker-queued CallAfter can land on a dead GUI:
        //
        //   1. Normal app close: MainFrame::~MainFrame() nulls
        //      plater_ before ~GUI_App() resets the scan client, so
        //      pending CallAfters processed in between would crash
        //      on app.sidebar() / app.notification_manager() which
        //      dereference plater_ (codex P1 on PR #13).
        //   2. Language-switch recreate_GUI(): mainframe->shutdown()
        //      runs while the worker is still active, and a brief
        //      window exists before the new MainFrame is wired.
        //
        // Bail early if any of the surfaces we need are gone.
        if (app.preset_bundle == nullptr) {
            BOOST_LOG_TRIVIAL(warning) << "[FilamentDB] preset_bundle is null — preset switch skipped";
            return;
        }
        if (app.plater() == nullptr) {
            BOOST_LOG_TRIVIAL(debug) << "[FilamentDB] plater is null (shutdown / GUI rebuild in progress) — preset switch skipped";
            return;
        }

        const Preset* preset = app.preset_bundle->filaments.find_preset(preset_name);
        if (preset == nullptr) {
            BOOST_LOG_TRIVIAL(info)
                << "[FilamentDB] no matching filament preset for '" << preset_name
                << "' — the slicer hasn't synced this filament yet";
            return;
        }

        TabFilament* fil_tab = dynamic_cast<TabFilament*>(app.get_tab(Preset::TYPE_FILAMENT));
        if (fil_tab == nullptr) {
            BOOST_LOG_TRIVIAL(warning) << "[FilamentDB] filament Tab is null — preset switch skipped";
            return;
        }

        // Target the Filament tab's currently-active extruder rather
        // than always extruder 0. On multi-extruder setups the tab
        // can be focused on a slot other than 0, and writing to both
        // 0 and the active slot would silently overwrite two
        // assignments per scan (codex P1 on PR #13). The sidebar's
        // dropdown flow uses this same rule.
        const int target_extruder = fil_tab->get_active_extruder();
        const std::size_t target_idx =
            target_extruder < 0 ? 0 : static_cast<std::size_t>(target_extruder);

        // The user-visible "Filament:" combobox in the sidebar reflects
        // the per-extruder assignment (`extruders_filaments[idx]`), not
        // the Filament tab's edit pointer (`filaments` collection).
        // Snapshot the extruder's current filament before the switch so
        // the log + notification can compare against it.
        const Preset* before_preset =
            (target_idx < app.preset_bundle->extruders_filaments.size())
                ? app.preset_bundle->extruders_filaments[target_idx].get_selected_preset()
                : nullptr;
        const std::string before = before_preset ? before_preset->name : std::string{};

        // Mirror the path the sidebar dropdown takes when the user
        // changes the filament:
        //   1. set_filament_preset(idx, name)  — assigns the extruder.
        //                                         No compat filter.
        //   2. tab->select_preset(name)        — updates the Filament
        //                                         tab. May silently
        //                                         fall back if the
        //                                         preset isn't on the
        //                                         active printer's
        //                                         compatible_printers
        //                                         list.
        //   3. update_all_filament_comboboxes() — refresh sidebar UI.
        //
        // The Sidebar reverts step 1 when step 2 fails. We don't —
        // for a scanned tag the user's intent is "use this filament",
        // so accept the extruder assignment even if the Filament tab
        // can't show it (the user will see the compat-warning toast
        // and can fix the compatible_printers list later).
        app.preset_bundle->set_filament_preset(target_idx, preset_name);
        fil_tab->select_preset(preset_name);
        app.sidebar().update_all_filament_comboboxes();

        // Re-read both the extruder assignment and the tab's selection
        // so we can tell three apart:
        //   (a) clean switch         — extruder = name, tab = name
        //   (b) extruder-only switch — extruder = name, tab fell back
        //                              (the preset isn't on the active
        //                               printer's compatible list)
        //   (c) total failure        — extruder ≠ name
        const Preset* after_extruder_preset =
            (target_idx < app.preset_bundle->extruders_filaments.size())
                ? app.preset_bundle->extruders_filaments[target_idx].get_selected_preset()
                : nullptr;
        const std::string after_extruder =
            after_extruder_preset ? after_extruder_preset->name : std::string{};
        const std::string after_tab = app.preset_bundle->filaments.get_selected_preset().name;

        // Persist the selection to config.ini on a clean switch — same
        // behaviour as the sidebar dropdown, so a scan-driven change
        // survives the next slicer restart (codex P2 on PR #13).
        // Skipped on the partial / failed paths to avoid persisting
        // a preset the user can't actually use yet.
        auto persist_selection = [&app]() {
            if (app.app_config != nullptr)
                app.preset_bundle->export_selections(*app.app_config);
        };

        if (after_extruder == preset_name && after_tab == preset_name) {
            BOOST_LOG_TRIVIAL(info)
                << "[FilamentDB] switched filament preset from '" << before
                << "' to '" << preset_name << "' on extruder " << target_extruder;
            persist_selection();
            return;
        }

        if (after_extruder == preset_name) {
            // Extruder accepted it, tab refused. Surface as a warning
            // so the user knows their filament IS loaded but isn't on
            // the printer's compatible list.
            BOOST_LOG_TRIVIAL(info)
                << "[FilamentDB] switched extruder " << target_extruder
                << " filament to '" << preset_name
                << "' (Filament tab fell back to '" << after_tab
                << "' because the preset isn't marked compatible with the active printer)";
            persist_selection();
            if (NotificationManager* nm = app.notification_manager()) {
                nm->push_notification(
                    NotificationType::CustomNotification,
                    NotificationManager::NotificationLevel::WarningNotificationLevel,
                    "Filament DB: loaded '" + preset_name +
                    "' on extruder " + std::to_string(target_extruder + 1) +
                    ". It is not marked as compatible with the active printer — "
                    "open the filament's settings and tick this printer under "
                    "'Compatible printers' to silence this warning.");
            }
            return;
        }

        BOOST_LOG_TRIVIAL(warning)
            << "[FilamentDB] could not select '" << preset_name
            << "' — the preset exists in the bundle but is not in the active "
            << "extruder's filament list (extruder " << target_extruder
            << " still on '" << after_extruder
            << "', Filament tab still on '" << after_tab
            << "'). This usually means the preset's 'compatible_printers' "
            << "list doesn't include the active printer.";
        if (NotificationManager* nm = app.notification_manager()) {
            nm->push_notification(
                NotificationType::CustomNotification,
                NotificationManager::NotificationLevel::WarningNotificationLevel,
                "Filament DB: could not load '" + preset_name +
                "' on the active printer. Open the Filaments tab, select "
                "'" + preset_name + "', and under 'Profile dependencies' check "
                "the box next to your printer in 'Compatible printers'. Save, "
                "then scan again.");
        }
    });
}

void FilamentScanClient::run()
{
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "[FilamentDB] curl_easy_init failed; scan client disabled";
        return;
    }

    // `?replay=0` suppresses the on-connect replay of the most recent
    // scan. We want only live tag reads to drive a preset switch —
    // otherwise opening the slicer minutes after a scan silently
    // clobbers the active preset (and any unsaved edits to it).
    const std::string url = m_base_url + "/api/scan/stream?replay=0";
    BOOST_LOG_TRIVIAL(info) << "[FilamentDB] scan client subscribing to " << url;

    using namespace std::chrono_literals;
    auto       backoff     = 500ms;
    const auto max_backoff = 30s;
    int        attempt     = 0;
    // Give-up gate for users who don't actually run Filament DB.
    // `filamentdb_url` defaults to localhost:3456 on a fresh install
    // (AppConfig.cpp), so without this we'd hammer localhost forever
    // on every PrusaSlicer launch even when there's no Filament DB
    // process to connect to (codex P2 on PR #13). Once we've had at
    // least one successful session we keep retrying on disconnect
    // forever (back to the normal robust-reconnect behaviour).
    bool       ever_connected   = false;
    int        startup_failures = 0;
    constexpr int MAX_STARTUP_FAILURES = 7; // ~60 s of total elapsed wait

    while (! m_stop.load()) {
        ++attempt;
        if (attempt > 1)
            BOOST_LOG_TRIVIAL(info) << "[FilamentDB] reconnecting to " << url << " (attempt " << attempt << ")";
        StreamState st;
        st.self = this;

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Accept: text/event-stream");

        curl_easy_reset(curl);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &scan_stream_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &st);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);          // no overall cap
        // Don't use CURLOPT_LOW_SPEED_LIMIT — the server's heartbeat
        // is `: hb` = 5 bytes every 25 s = 0.2 B/s average, well
        // below any usable byte-rate threshold (codex P2 on PR #13).
        // Detect dead connections via TCP keepalive instead, which
        // operates at the socket layer and doesn't care about the
        // application-level byte rate.
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE,  60L); // start probing after 60 s of inactivity
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 30L); // probe every 30 s if unacked
        // Honour cooperative cancellation between bytes.
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &scan_stream_xferinfo_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &m_stop);

        const auto    session_start = std::chrono::steady_clock::now();
        const CURLcode rc            = curl_easy_perform(curl);
        const auto    session_dur   = std::chrono::steady_clock::now() - session_start;
        curl_slist_free_all(headers);

        if (m_stop.load()) break;

        const bool healthy_session = session_dur >= std::chrono::seconds(5);
        if (healthy_session) {
            // Reset the reconnect backoff so a long-healthy stream
            // that drops once doesn't inherit an old outage's grown
            // backoff (codex P2 on PR #13). 5 s is short enough to
            // consider any successful initial handshake + prelude
            // flow "healthy" and long enough to filter out fast
            // connection failures.
            backoff          = std::chrono::milliseconds(500);
            ever_connected   = true;
            startup_failures = 0;
        } else if (! ever_connected) {
            // Never connected — count toward the give-up threshold.
            ++startup_failures;
            if (startup_failures >= MAX_STARTUP_FAILURES) {
                BOOST_LOG_TRIVIAL(info)
                    << "[FilamentDB] " << url << " not reachable after "
                    << startup_failures
                    << " attempts; scan client giving up for this session. "
                    << "Restart PrusaSlicer once Filament DB is running.";
                break;
            }
        }

        if (rc != CURLE_OK) {
            BOOST_LOG_TRIVIAL(debug)
                << "[FilamentDB] scan stream disconnected (" << curl_easy_strerror(rc)
                << "); reconnecting after " << backoff.count() << "ms";
        }

        // Interruptible sleep — wake on stop() without waiting up to
        // 30 s for the next tick (codex P1 on PR #13). Using a CV
        // with the atomic m_stop as the predicate so the curl
        // xferinfo callback (lock-free) still works unchanged.
        {
            std::unique_lock<std::mutex> lk(m_stop_mtx);
            m_stop_cv.wait_for(lk, backoff, [this] { return m_stop.load(); });
        }
        backoff = std::min<std::chrono::milliseconds>(backoff * 2, max_backoff);
    }

    curl_easy_cleanup(curl);
    BOOST_LOG_TRIVIAL(info) << "[FilamentDB] scan client stopped";
}

} } // namespace Slic3r::GUI
