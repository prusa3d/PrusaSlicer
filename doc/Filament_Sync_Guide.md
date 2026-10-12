# Filament Sync Guide (INDX)

This guide explains **Sync Filaments from Printer**: it reads the filament loaded in each tool of
a CORE One INDX over PrusaLink, then selects a matching filament preset and sets the color for
each extruder. Nothing changes until you click **Apply** in its preview.

It's one way only. Nothing is sent to the printer or to Filament DB.

---

## Contents

1. [What you need](#1-what-you-need)
2. [Running it](#2-running-it)
3. [How a preset is picked](#3-how-a-preset-is-picked)
4. [Extruder colors](#4-extruder-colors)
5. [Notes in the preview](#5-notes-in-the-preview)
6. [Messages](#6-messages)
7. [How it works](#7-how-it-works)

---

## 1. What you need

- **A printer that reports its filaments.** A CORE One INDX with firmware that has Filament
  Slots: hyiger/Prusa-Firmware-Buddy `v6.9.2-hy.3` or later. Stock Prusa firmware doesn't report
  them, and neither does the CORE One L INDX build yet.
- **A physical printer** for your INDX printer profile, with **PrusaLink** as its host type, the
  printer's address, and its API key or user and password. Add one with the cog icon next to the
  Printer list: **Add physical printer**. Then select it in the Printer list.
- The **sync icon** (two circular arrows) appears next to **Filament** in the sidebar once that
  physical printer is selected.

## 2. Running it

1. Click the sync icon next to **Filament**, or choose **Maintenance → Sync Filaments from
   Printer…**.
2. PrusaSlicer reads the printer, which takes a moment (it gives up after 10 seconds).
3. The preview lists each tool the printer reports:
   - **In the printer**: the vendor, material and color the printer has for that tool.
   - **Preset**: what happens to the extruder's filament preset: *Keeps*, *Selects*, *Installs
     and selects*, *Creates … from …*, or *No change* for an empty tool.
   - **Preset color**: the filament color of that preset, which is what the extruder shows.
4. Untick any row you don't want, then click **Apply**.

Tool N is always extruder N. A dock that isn't calibrated isn't reported, and its extruder is
left alone; the other tools don't shift down to fill the gap.

If the Filament tab has unsaved changes and its extruder's preset is about to change, PrusaSlicer
asks first, as when you pick another preset yourself.

## 3. How a preset is picked

For each loaded tool, among the filament presets compatible with that extruder:

1. **Vendor and material must match.** Vendor names are compared without case or punctuation, so
   `3D-Fuel` matches `3D Fuel` and `eSun` matches `Esun`, and the printer's `Prusament` matches
   PrusaSlicer's `Prusa Polymers`. Materials are compared without case; `FLEX`, `TPU`, `TPE` and
   `PEBA` count as one, and a type the printer cut to 7 characters still matches (`PPS-CF1`
   matches `PPS-CF10`). `PETG` and `PET` stay apart.
2. **The current preset is kept** when it fits as well as the best one. The printer reports only
   vendor, material and color, so it can't tell plain from matte, silk or carbon fiber filament:
   if you picked `Overture PLA Matte White` for a white Overture PLA, it stays.
3. Otherwise the **best fit** is selected:
   - On a nozzle that isn't hardened, presets for abrasive filament (marked abrasive, or carbon or
     glass fiber by name) come last.
   - A preset named after the tool's color, or with exactly its color, comes first; then presets
     without a color in their name; then other colors. `Gray` and `Grey` are the same color, and
     a preset named by a hex code (`Polymaker PETG #1A2B3C`) is that color. In the name of one of
     your own presets that starts with the tool's vendor and material (in any spelling that
     matches), as new presets' names do, the words after the material are another color when the
     preset's filament color differs from the tool's: for a red tool, `Overture PETG Space` and a
     `Polymaker PETG Clear` made for a user color are other colors. In a system preset those
     words name a variant the printer can't tell apart (`Prusament PETG Carbon Fiber`), and so
     they do in your copy of one (`Prusament PLA Blend Tuned`).
   - Then the preset whose name says the least beyond vendor, material and color:
     `Overture PLA White` before `Overture PLA Silk White`.
   - Then the closest filament color, then your own presets before system ones.
4. **Not installed:** a matching system preset you haven't installed in the Configuration Wizard
   is installed and selected when no installed preset fits, or when the installed ones are all
   another color and it fits better.
5. **Nothing matches, or only other colors:** when the printer names a vendor and none of its
   presets of that material fits, or only ones in another color do (`Polymaker PETG Black` for a
   red Polymaker PETG), a user preset is created, named `<vendor> <material> <color>` (for
   example `Polymaker PETG Red`; a printer user color without a name goes by its hex, and a tool
   without a color gets no color in the name), copied from a preset of the same material that
   suits the extruder: the extruder's current one, else a Generic one, else Prusament. Its vendor
   and filament color come from the printer. If that name is taken, it gets ` @INDX`. Tools with
   the same vendor, material and color share one new preset.
   It's saved with your other user presets; it isn't added to Filament DB. When no preset can be
   made (nothing suits the extruder to start from), the closest color is selected instead, and
   a note says why.

When the printer reports no vendor (the filament was loaded by G-code, or the vendor was
cleared), the current preset is kept if it's the right material, unless it's for abrasive
filament on a nozzle that isn't hardened; otherwise a Generic one is selected. Nothing is created
without a vendor. A tool without a color doesn't count against a preset that names one.

### Empty tools

An empty tool keeps its extruder's preset. PrusaSlicer remembers which tools were empty, and which
docks weren't calibrated, at the last sync with the selected physical printer:

- **The sidebar hides their filament lists**, as Bambu Studio and OrcaSlicer show only loaded
  filaments, and numbers the lists it still shows, so a gap (tools 1, 2, 3 and 5) stays clear.
  Untick **Hide empty extruders in the sidebar** in the preview to show them all; that choice is
  remembered. Every list shows again when you select another physical printer. The extruders
  themselves stay: the INDX start G-code names all eight tools.
- **A print that uses one of them gets a warning** naming those extruders and the time of the
  sync: load the filament, or move those parts to another extruder.

Neither is kept after you close PrusaSlicer, since the printer can change meanwhile; sync again
to refresh them.

## 4. Extruder colors

The colors come from the filament presets: a preset named for a color, or with the tool's exact
color, is preferred (section 3), and a new preset gets the tool's color as its filament color.

When an extruder has a color of its own, the preview offers **Reset extruder colors to filament
colors** (ticked by default), which does what **Reset to Filament Color** does for each extruder
in the printer settings: every extruder then shows its filament's color in the sidebar, the
preview and the G-code. Extruder colors belong to the **printer preset**, so it shows as modified
afterwards; save it as a user preset to keep that.

If a selected preset's color isn't the tool's (no preset of that vendor and material has it), the
preview says so in a note, and the extruder shows the preset's color.

## 5. Notes in the preview

- **Nozzle differences:** the tool's nozzle doesn't match the printer profile (diameter, or high
  flow). The sync doesn't switch printer profiles; pick the right one yourself.
- **Abrasive:** the only matching preset is for abrasive filament and the nozzle isn't hardened.
- **No vendor**, or one of the printer's unnamed vendors (`VENDOR1` to `VENDOR8`).
- **Closest color:** no preset of that vendor and material has the tool's color, so another
  color's preset is selected, and the extruder shows that preset's color.
- **Tool N isn't in the printer profile:** for example an 8-tool printer with a 4-tool profile.
- **Couldn't read** some of what the printer sent: those tools are skipped.

## 6. Messages

| Message | What to do |
| --- | --- |
| *To read the filaments loaded in your printer, select a physical printer…* | Add and select a PrusaLink physical printer (section 1). |
| *… doesn't report the filaments in its tools* | The printer's firmware has no Filament Slots. |
| *… refused the API key or password* | Check them in the physical printer's settings, and that login is enabled on the printer. |
| *Couldn't reach the printer at …* | Check the address and that the printer is on the network. |
| *… reports no tools* | No dock is calibrated yet. |

## 7. How it works

The printer serves `GET /api/v1/tools` over PrusaLink (Digest or API key, like uploads):

```json
{"tools":{"1":{"loaded":true,"material":"PLA","vendor":"Prusament","color":"Orange","color_hex":"#F8651B",
"nozzle_diameter":0.40,"hardened":false,"high_flow":false}, ...}}
```

- `src/slic3r/Utils/PrinterTools.{hpp,cpp}`: fetching and parsing (`fetch_printer_tools`,
  `PrintHost::get_tools`).
- `src/slic3r/Utils/FilamentSync.{hpp,cpp}`: matching (`make_plan`) and applying
  (`apply_presets`), with no wx dependency.
- `src/slic3r/GUI/FilamentSyncDialog.{hpp,cpp}`: the sidebar button, the menu item and the
  preview.

Tests: `[PrinterTools]` and `[FilamentSync]` in `tests/slic3rutils`, and the hidden
`[PrusaLinkMock]` tests, which run against
[prusalink-mock](https://github.com/hyiger/prusalink-mock), a PrusaLink server that answers like
the firmware. CMake registers them when it finds Python 3.10 or newer and a prusalink-mock checkout
(`PRUSALINK_MOCK_DIR`, or `../prusalink-mock` next to this repository):

```sh
ctest --test-dir build-arm64 -L 'prusalink_mock|filament_sync' --output-on-failure
```

To try the feature without a printer, run the mock and point a physical printer at it:

```sh
PYTHONPATH=../prusalink-mock/src python3 -m prusalink_mock serve
```

Then add a physical printer with host type PrusaLink, address `http://127.0.0.1:8088` and API key
`mockKeyTest2345`, for an INDX printer profile. `--scenario indx-gaps` and the other scenarios
change what it reports.
