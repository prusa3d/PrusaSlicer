# Preset Updater — Manual Test

Follow these steps **in order** against a fresh throwaway data directory. Each step is a
command plus the result you should see (printed JSON and files on disk). The automated tests
run against a mock server; this pass exercises the real production endpoint, the real
download/install pipeline, and the files written to disk.

## Setup

1. Build a normal binary (GUI + launcher).
2. Use a **throwaway data directory** and pass `--datadir <tmp>` on every command, pointing at
   an empty folder, e.g. `--datadir C:\tmp\pu_manual`. This keeps your real profiles safe.
3. Endpoint: default is production `https://preset-repo-api.prusa3d.com` (no action needed).
   To point elsewhere, set `PRUSA_PRESET_REPO_URL` (only URLs on Prusa domains are accepted:
   `prusa.com`, `prusa3d.com`, `prusa.cz`, `prusa3d.cz`, `printables.com`, `testprusaverse.com`).
4. Only **one** `--preset-update*` action may be passed per invocation.
5. Examples below use `prusa-slicer`; substitute your platform's binary name.

### Where results land

| Output | Meaning |
| --- | --- |
| stdout | JSON result of the action. A failure is an object with an `"error"` key. |
| `<datadir>/shared_runtime/RepositoryManifest.json` | The sources list and their `selected` flags |
| `<datadir>/update_sync/<repo_id>/…` | Files staged (downloaded) but not yet installed |
| `<datadir>/presets/local/<repo_id>/<Vendor>/` + `<Vendor>.idx` | Installed vendor profiles |
| `<datadir>/local_repositories/<uuid>/` | Unzipped local (offline) sources |

## Steps

### 1. List sources
```
prusa-slicer --datadir <tmp> --preset-update-sources
```
- [ ] Prints `{"result": [ … ]}` — a JSON array of sources fetched from the server.
- [ ] The Prusa production source is present with `"selected": true`.
- [ ] `shared_runtime/RepositoryManifest.json` now exists and matches the printed list.
- [ ] **Copy the Prusa source's `uuid`** from the output; later steps call it `<PRUSA_UUID>`.

### 2. Stage and list reconfigurations (clean datadir)
```
prusa-slicer --datadir <tmp> --preset-update-list
```
- [ ] Prints `{"result": { … }}` where `new_vendors` lists the online vendors and the other
      arrays (`regular_updates`, `forced_updates`, `forced_downgrades`, `not_in_index`) are empty.
- [ ] Files appear under `update_sync/<repo_id>/`.
- [ ] No `"error"` in the output.

### 3. List again (still staged, not installed)
```
prusa-slicer --datadir <tmp> --preset-update-list
```
- [ ] Same `new_vendors` result as step 2 — staging does **not** install, so the vendors are
      still "new". No `"error"`.

### 4. Install
```
prusa-slicer --datadir <tmp> --preset-update
```
- [ ] No `"error"`. On full success this command prints **nothing** (or only a
      `{"warnings": […]}` object).
- [ ] `presets/local/<repo_id>/<Vendor>/vendor.yaml` and `presets/local/<repo_id>/<Vendor>.idx`
      now exist.
- [ ] The installed `vendor.yaml` `version` equals the `recommended_version` shown in step 2.
- [ ] There are no files in `update_sync/<repo_id>/`.

### 5. List after install
```
prusa-slicer --datadir <tmp> --preset-update-list
```
- [ ] Prints `{"result": { … }}` with **all** arrays empty — everything is up to date.
- [ ] There are no files in `update_sync/<repo_id>/`.

### 6. Deselect the source
```
prusa-slicer --datadir <tmp> --preset-update-select-source <PRUSA_UUID>
```
- [ ] Prints `{"result": [ … ]}` where the entry for `<PRUSA_UUID>` now has `"selected": false`
      (this command toggles the flag).
- [ ] In `RepositoryManifest.json`, that `uuid`'s `"selected"` is now `false`.

### 7. List with the source deselected
```
prusa-slicer --datadir <tmp> --preset-update-list
```
- [ ] Prints `{"result": { … }}` with all arrays empty — a deselected source is not synced.

### 8. Re-select the source
```
prusa-slicer --datadir <tmp> --preset-update-select-source <PRUSA_UUID>
```
- [ ] The entry for `<PRUSA_UUID>` is `"selected": true` again.
- [ ] Selecting a source automatically deselects any **other** source that shares the same `id`.

### 9. Add a local (offline) source with the same `id` as the online source
Use a local zip whose repository `id` matches the online Prusa source's `id`, so the two are
mutually exclusive (if the `id`s differ, both stay selected and nothing switches off).
(Optional: delete the datadir first so the fresh offline source carries the same update you
installed in step 4.)
```
prusa-slicer --datadir <tmp> --preset-update-add-local <path-to-repo.zip>
```
- [ ] Prints `{"result": [ … ]}` that now includes the local source (non-empty `zip_path` and
      `unzipped_data_path`), with `"selected": true`.
- [ ] The online source with the same `id` is now `"selected": false` — adding a local source
      deselects other sources with the same `id`.
- [ ] Its unzipped data appears under `local_repositories/<uuid>/`.
- [ ] **Note both uuids** for this `id`: `<ONLINE_UUID>` (online, now deselected) and
      `<LOCAL_UUID>` (local, selected). They are the same as `<PRUSA_UUID>`/the add output unless
      you deleted the datadir — re-run `--preset-update-sources` if unsure.

### 10. Select the online source — switches the local one off
```
prusa-slicer --datadir <tmp> --preset-update-select-source <ONLINE_UUID>
```
- [ ] `<ONLINE_UUID>` becomes `"selected": true`.
- [ ] `<LOCAL_UUID>` (same `id`) becomes `"selected": false` — selecting a source switches off
      the other source with the same `id`.

### 11. Select the local source — switches the online one off
```
prusa-slicer --datadir <tmp> --preset-update-select-source <LOCAL_UUID>
```
- [ ] `<LOCAL_UUID>` becomes `"selected": true`.
- [ ] `<ONLINE_UUID>` becomes `"selected": false`.

### 12. Install from the local source (optional)
```
prusa-slicer --datadir <tmp> --preset-update-list
prusa-slicer --datadir <tmp> --preset-update
```
- [ ] `--preset-update-list` reports the local source's vendor(s) as reconfigurations.
- [ ] `--preset-update` installs them under `presets/local/`.

### 13. Remove the local source
```
prusa-slicer --datadir <tmp> --preset-update-remove-local <LOCAL_UUID>
```
- [ ] The printed `{"result": [ … ]}` no longer contains `<LOCAL_UUID>`.
- [ ] `local_repositories/<LOCAL_UUID>/` is deleted.

### 14. Cleanup staged files
```
prusa-slicer --datadir <tmp> --preset-update-cleanup
```
- [ ] Prints `{"result": { … }}` (a reconfiguration check after cleanup).
- [ ] Everything under `update_sync/` is removed.
- [ ] Already-installed profiles under `presets/local/` are left untouched.

### 15. GUI smoke test (best effort)
- [ ] Launch the GUI build against the same `<datadir>` and confirm the updater
      notifications/dialogs appear and behave, and that startup does not crash.

## Pass / fail

- **Pass**: every step's stdout has no `"error"` key, the on-disk state matches each check, and
  nothing crashes.
- **Fail**: any `{"error": …}`, a missing or incorrect file, or a crash.

## On failure — what to attach

- The exact command line (including `--datadir`).
- The full stdout (JSON).
- The relevant subtree of `<datadir>` (`update_sync/`, `presets/local/`,
  `shared_runtime/RepositoryManifest.json`, `local_repositories/`).
- Relevant log lines, especially the `target:… attempt:… delay:…` lines emitted during downloads.
