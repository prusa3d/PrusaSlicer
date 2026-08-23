// Copyright (c) 2026
// PrusaSlicer is released under the terms of the AGPLv3 or higher
//
// Tests for the SSE record parser in FilamentScanClient. Codex flagged
// (PR #13 P1) that the original LF-only split silently dropped events
// from a standards-compliant CRLF publisher. The parser now normalises
// line endings before splitting on the blank-line record separator —
// these tests pin the LF, CR, and CRLF paths, plus a CRLF straddling
// two write-callback chunks.

#include <catch2/catch_test_macros.hpp>

#include "slic3r/GUI/FilamentScanClient.hpp"

#include <vector>

using namespace Slic3r::GUI::filament_scan_detail;

namespace {

// Convenience: feed an entire string in one chunk and collect events.
std::vector<ParsedEvent> feed_all(const std::string& s)
{
    SseRecordParser           parser;
    std::vector<ParsedEvent>  events;
    parser.feed(s.data(), s.size(),
                [&](const ParsedEvent& ev) { events.push_back(ev); });
    return events;
}

} // namespace

TEST_CASE("SseRecordParser: LF line endings (RFC EventSource default)", "[FilamentScanClient]")
{
    auto events = feed_all("event: scan\ndata: hello\n\n");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].event_type == "scan");
    REQUIRE(events[0].data       == "hello");
}

TEST_CASE("SseRecordParser: CRLF line endings (Windows / many servers)", "[FilamentScanClient]")
{
    // The whole record uses \r\n and ends in \r\n\r\n. Before the
    // parser normalised, this silently produced zero events.
    auto events = feed_all("event: scan\r\ndata: hello\r\n\r\n");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].event_type == "scan");
    REQUIRE(events[0].data       == "hello");
}

TEST_CASE("SseRecordParser: bare-CR line endings (legacy Mac)", "[FilamentScanClient]")
{
    // Vanishingly rare in practice, but the EventSource spec lists it
    // alongside LF/CRLF, so cover it for completeness.
    auto events = feed_all("event: scan\rdata: hello\r\r");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].event_type == "scan");
    REQUIRE(events[0].data       == "hello");
}

TEST_CASE("SseRecordParser: CRLF straddling write-callback chunks", "[FilamentScanClient]")
{
    // libcurl can deliver bytes in arbitrary-sized chunks. A CRLF
    // that crosses a chunk boundary must still normalise to a single
    // LF — otherwise the buffer accumulates spurious blank lines.
    SseRecordParser parser;
    std::vector<ParsedEvent> events;
    auto emit = [&](const ParsedEvent& ev) { events.push_back(ev); };

    const std::string s = "event: scan\r\ndata: hello\r\n\r\n";

    for (std::size_t boundary = 1; boundary < s.size(); ++boundary) {
        SECTION("split at byte " + std::to_string(boundary))
        {
            events.clear();
            SseRecordParser p;
            p.feed(s.data(),            boundary,             emit);
            p.feed(s.data() + boundary, s.size() - boundary, emit);
            REQUIRE(events.size() == 1);
            REQUIRE(events[0].event_type == "scan");
            REQUIRE(events[0].data       == "hello");
        }
    }
}

TEST_CASE("SseRecordParser: multi-line data is joined with LFs", "[FilamentScanClient]")
{
    // Per the EventSource spec, successive `data:` lines in a single
    // record are concatenated with a single LF between them, with no
    // trailing LF. Filament DB sends compact single-line JSON so
    // this is rarely exercised in production, but the parser must
    // honour the spec for interoperability with other publishers
    // (codex P2 on PR #13).
    auto events = feed_all("data: part1\ndata: part2\n\n");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].event_type == "message");
    REQUIRE(events[0].data       == "part1\npart2");
}

TEST_CASE("SseRecordParser: three data: lines yield two LFs", "[FilamentScanClient]")
{
    auto events = feed_all("data: a\ndata: b\ndata: c\n\n");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "a\nb\nc");
}

TEST_CASE("SseRecordParser: leading empty data line preserved", "[FilamentScanClient]")
{
    // EventSource spec: every `data:` field contributes a LF to the
    // buffer (even when the value is empty), with the final trailing
    // LF stripped at dispatch. The shortcut "only insert LF if buffer
    // is non-empty" would lose this leading LF and corrupt payloads
    // like JSON intentionally prefixed with a newline (codex P2 on
    // PR #13).
    auto events = feed_all("data:\ndata: {\"x\":1}\n\n");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "\n{\"x\":1}");
}

TEST_CASE("SseRecordParser: trailing data: LF is stripped, not duplicated",
          "[FilamentScanClient]")
{
    // Spec calls for trimming a single LF after the last data line,
    // not all trailing LFs — a deliberate trailing empty data:
    // produces a real LF in the payload.
    auto events = feed_all("data: a\ndata:\n\n");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == "a\n");
}

TEST_CASE("SseRecordParser: heartbeat comment lines are ignored", "[FilamentScanClient]")
{
    // Filament DB sends `: hb` every 25s to keep proxies awake. The
    // record (a single comment line followed by a blank line) carries
    // no data, so no event should be emitted.
    auto events = feed_all(": hb\n\n");
    REQUIRE(events.empty());
}

TEST_CASE("SseRecordParser: retry: lines are ignored", "[FilamentScanClient]")
{
    // We run our own reconnect loop with exponential backoff, so
    // the spec-defined `retry:` field is intentionally a no-op.
    // It must not look like an `event:` or `data:` field by accident.
    auto events = feed_all("retry: 5000\nevent: scan\ndata: x\n\n");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].event_type == "scan");
    REQUIRE(events[0].data       == "x");
}

TEST_CASE("SseRecordParser: incomplete record buffers without emitting", "[FilamentScanClient]")
{
    // A chunk that doesn't yet terminate in a blank line must keep
    // accumulating in the buffer — no event yet.
    SseRecordParser parser;
    std::vector<ParsedEvent> events;
    auto emit = [&](const ParsedEvent& ev) { events.push_back(ev); };
    parser.feed("event: scan\ndata: hello", 23, emit);
    REQUIRE(events.empty());
    // Now finish it.
    parser.feed("\n\n", 2, emit);
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].event_type == "scan");
    REQUIRE(events[0].data       == "hello");
}

TEST_CASE("SseRecordParser: two records back-to-back in one chunk", "[FilamentScanClient]")
{
    auto events = feed_all(
        "event: scan\ndata: first\n\n"
        "event: scan\ndata: second\n\n");
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].data == "first");
    REQUIRE(events[1].data == "second");
}

TEST_CASE("SseRecordParser: leading-space stripping after colon", "[FilamentScanClient]")
{
    // SSE spec: a single space after the colon is part of the field
    // syntax and must be stripped. Multiple spaces or tabs are
    // preserved as data.
    auto events = feed_all("event: scan\ndata:  spaced\n\n");
    REQUIRE(events.size() == 1);
    REQUIRE(events[0].data == " spaced");
}

TEST_CASE("parse_record: bare `event:` (no space) parses correctly", "[FilamentScanClient]")
{
    const ParsedEvent ev = parse_record("event:scan\ndata:x");
    REQUIRE(ev.event_type == "scan");
    REQUIRE(ev.data       == "x");
}

TEST_CASE("FilamentScanClient: ctor strips trailing slashes from base URL",
          "[FilamentScanClient]")
{
    using Slic3r::GUI::FilamentScanClient;

    // No slash — passthrough.
    REQUIRE(FilamentScanClient("http://host:3456").base_url()
            == "http://host:3456");

    // Single trailing slash — codex P2 case. Without the strip the
    // scan-stream URL would be "http://host:3456//api/scan/stream"
    // and miss strict route matches.
    REQUIRE(FilamentScanClient("http://host:3456/").base_url()
            == "http://host:3456");

    // Multiple trailing slashes — all stripped.
    REQUIRE(FilamentScanClient("http://host:3456///").base_url()
            == "http://host:3456");

    // No path component — defensive; an all-slashes URL is nonsense
    // but the ctor mustn't infinite-loop or crash.
    REQUIRE(FilamentScanClient("///").base_url().empty());
}
