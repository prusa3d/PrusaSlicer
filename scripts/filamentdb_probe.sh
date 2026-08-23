#!/usr/bin/env bash
# Probe a FilamentDB server to see how it handles a brand-new filament name.
# Run this on a host that can resolve hyiger.local.
#
# Usage:  ./filamentdb_probe.sh [URL]
#   URL defaults to http://hyiger.local:3456

set -u
URL=${1:-http://hyiger.local:3456}
NAME="Probe Test New PLA $(date +%s)"
ENC_NAME=$(printf '%s' "$NAME" | sed 's/ /%20/g')
JSON='{"name":"'"$NAME"'","config":{"filament_type":"PLA","temperature":"210"}}'

echo "Probing FilamentDB at: $URL"
echo "Test filament:        $NAME"
echo

step() { printf "\n--- %s ---\n" "$1"; }

step "Reachability — GET /api/filaments/prusaslicer"
curl -sS -o /dev/null -w "HTTP %{http_code}  (size_download=%{size_download})\n" --max-time 5 \
  "$URL/api/filaments/prusaslicer" || echo "(connection failed)"

step "Pre-check — GET /api/filaments/$ENC_NAME"
PRE=$(curl -sS -o /dev/null -w "%{http_code}" --max-time 5 \
  "$URL/api/filaments/$ENC_NAME?nozzle_diameter=0.4&high_flow=0")
echo "HTTP $PRE  (expect 404 — filament should not exist yet)"

step "POST /api/filaments/$ENC_NAME?nozzle_diameter=0.4&high_flow=0"
POST=$(curl -sS -o /tmp/fdb_post.body -w "%{http_code}" --max-time 5 -X POST \
  -H "Content-Type: application/json" \
  -d "$JSON" \
  "$URL/api/filaments/$ENC_NAME?nozzle_diameter=0.4&high_flow=0")
echo "HTTP $POST"
echo "Response body:"
head -c 500 /tmp/fdb_post.body; echo

step "Verify — GET /api/filaments/$ENC_NAME"
VERIFY=$(curl -sS -o /tmp/fdb_get.body -w "%{http_code}" --max-time 5 \
  "$URL/api/filaments/$ENC_NAME")
echo "HTTP $VERIFY"
head -c 500 /tmp/fdb_get.body; echo

step "PUT (alternative upsert) /api/filaments/$ENC_NAME"
PUT=$(curl -sS -o /tmp/fdb_put.body -w "%{http_code}" --max-time 5 -X PUT \
  -H "Content-Type: application/json" \
  -d "$JSON" \
  "$URL/api/filaments/$ENC_NAME?nozzle_diameter=0.4&high_flow=0")
echo "HTTP $PUT"
head -c 500 /tmp/fdb_put.body; echo

step "Collection-create POST /api/filaments  (body has name)"
COLL=$(curl -sS -o /tmp/fdb_coll.body -w "%{http_code}" --max-time 5 -X POST \
  -H "Content-Type: application/json" \
  -d "$JSON" \
  "$URL/api/filaments")
echo "HTTP $COLL"
head -c 500 /tmp/fdb_coll.body; echo

step "Summary"
echo "Pre-existing GET: $PRE   (404 = filament absent before test, 200 = name collision)"
echo "POST /name:       $POST  (200/201 = upsert works on POST  /  404 = needs creation)"
echo "GET after POST:   $VERIFY  (200 = POST created it!  /  404 = POST didn't persist)"
echo "PUT /name:        $PUT   (200/201 = PUT works as upsert)"
echo "POST /collection: $COLL  (200/201 = REST create-on-collection works)"
echo
echo "Decision tree for what to wire into the client:"
echo "  - if 'POST /name' returns 2xx and GET shows the entry  →  sync as-is is correct"
echo "  - else if 'PUT /name' returns 2xx                       →  retry POST→PUT"
echo "  - else if 'POST /collection' returns 2xx                →  retry POST→collection"
echo "  - else                                                  →  server needs a create endpoint"
