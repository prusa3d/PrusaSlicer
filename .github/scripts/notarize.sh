#!/usr/bin/env bash
# Submit a file to Apple's notary service and wait for the verdict, retrying
# network failures. `notarytool submit --wait` gives up on the first dropped
# connection while polling, even though Apple already has the upload, which
# threw away a 3-hour macOS build. Here the upload is retried as a whole, and
# polling is retried against the same submission id.
#
# Usage: notarize.sh <file.zip|file.dmg>
# Env:   NOTARY_APPLE_ID, NOTARY_PASSWORD, NOTARY_TEAM_ID
set -euo pipefail

FILE="$1"
AUTH=(--apple-id "$NOTARY_APPLE_ID" --password "$NOTARY_PASSWORD" --team-id "$NOTARY_TEAM_ID")

json_field() { python3 -c "import json,sys; print(json.load(sys.stdin).get('$1',''))"; }

ID=""
for attempt in 1 2 3 4 5; do
  if OUT=$(xcrun notarytool submit "$FILE" "${AUTH[@]}" --output-format json); then
    ID=$(printf '%s' "$OUT" | json_field id)
    [ -n "$ID" ] && break
  fi
  echo "notarytool submit failed (attempt $attempt), retrying in 30 s"
  sleep 30
done
[ -n "$ID" ] || { echo "::error::Could not submit $FILE for notarization"; exit 1; }
echo "Submission id: $ID"

STATUS=""
for attempt in $(seq 1 20); do
  # wait returns once processing ends; a network error makes it exit nonzero early.
  xcrun notarytool wait "$ID" "${AUTH[@]}" --timeout 30m || true
  if INFO=$(xcrun notarytool info "$ID" "${AUTH[@]}" --output-format json); then
    STATUS=$(printf '%s' "$INFO" | json_field status)
    echo "Status: $STATUS"
    [ "$STATUS" != "In Progress" ] && break
  fi
  echo "Notarization still pending or unreachable (attempt $attempt), retrying in 30 s"
  sleep 30
done

if [ "$STATUS" != "Accepted" ]; then
  echo "::error::Notarization of $FILE ended with status '${STATUS:-unknown}'"
  xcrun notarytool log "$ID" "${AUTH[@]}" || true
  exit 1
fi
