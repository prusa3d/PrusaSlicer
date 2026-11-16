# Filament Sync Guide (INDX)

This guide explains **Sync Filaments from Printer**: it reads the filament loaded in each tool of
a CORE One INDX over PrusaLink, or the filament Prusa Connect has for each tool of your printer,
then selects a matching filament preset and sets the color for each extruder. Presets and extruder
colors don't change until you click **Apply** in its preview.

It's one way only. Nothing is sent to the printer, to Prusa Connect or to Filament DB.

For a short walkthrough with screenshots of the printer and slicer screens, see the
[Filament Slots and Sync user guide](Filament_Slots_and_Sync_User_Guide.md).

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

The sync reads one of two sources:

**From the printer, over PrusaLink** (on your network):

- **A printer that reports its filaments.** A CORE One INDX with firmware that has Filament
  Slots: hyiger/Prusa-Firmware-Buddy `v6.9.2-hy.3` or later. Stock Prusa firmware doesn't report
  them, and neither does the CORE One L INDX build yet.
- **A physical printer** for your INDX printer profile, with **PrusaLink** as its host type, the
  printer's address, and its API key or user and password. Add one with the cog icon next to the
  Printer list: **Add physical printer**. Then select it in the Printer list.

**From Prusa Connect** (anywhere):

- **Sign in to Prusa Account** in PrusaSlicer, with the printer registered in your Prusa Connect
  account. No physical printer is needed, and any firmware works.
- **Assign the filament in Connect**: open the printer's **Tools** page in Prusa Connect and pick
  each tool's material (brand, product and color). Without that, Connect knows only what the
  printer reports: the type and, from Filament Slots firmware, the color, but never the vendor.
- A Prusa printer profile (a system one, or one saved from it) whose **printer model** Connect
  lists for the printer (`COREONE_INDX8T` for an 8-tool INDX). Printers with an MMU aren't read
  yet.

When a PrusaLink physical printer is selected, the sync reads it; otherwise, when you're signed in,
it reads Prusa Connect. The **sync icon** (two circular arrows) appears next to **Filament** in
the sidebar whenever one of them can be read; its tooltip says which.

## 2. Running it

1. Click the sync icon next to **Filament**, or choose **Maintenance → Sync Filaments from
   Printer…**.
2. PrusaSlicer reads the printer, or Prusa Connect, which takes a moment (it gives up after 10
   seconds). When several printers in your Connect account fit the printer profile, it asks which
   one, starting with the one you picked last.
3. The preview lists each tool the printer reports:
   - **In the printer** (or **In Prusa Connect**): the vendor, material and color the printer has
     for that tool. A material assigned in Connect shows its brand and product name, such as
     `3D Fuel Pro PCTG, Translucent Cyan`.
   - **Preset**: what happens to the extruder's filament preset: *Keeps*, *Selects*, *Installs
     and selects*, *Creates … from …*, or *No change* for an empty tool.
   - **Preset color**: the filament color of that preset, which is what the extruder shows.
4. For a loaded tool, **Preset** is a list: the suggestion first, then a new preset in the
   tool's color, the extruder's other presets of that material (the tool's vendor's first, then
   that vendor's system presets you haven't installed, then other vendors'), and *Keeps …* or
   *No change* to leave the extruder as it is. Pick what you want for each, then click **Apply**.

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
   if you picked `Overture PLA Matte White` for a white Overture PLA, it stays. A product name
   from Prusa Connect can tell them apart (below).
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
     they do in a copy you saved from one under its name (`Prusament PLA Blend Tuned`).
     The filament color of one of your own presets counts as well, whatever its name: for a
     white tool, `PolyLite PLA Teal` is another color. Shades the printer's color names lump
     together, such as white and off-white, are the same color, and a color your preset still
     shares with the system preset it was saved from doesn't count.
   - From Prusa Connect, then the presets that agree with the product about carbon or glass fiber
     (`XPETG CF`, `PETG-CF`) by name or type, then the preset whose name is closest to the
     product name (below).
   - Then the preset whose name says the least beyond vendor, material, color and product:
     `Overture PLA White` before `Overture PLA Silk White`.
   - Then the closest filament color, then your own presets before system ones.
4. **Not installed:** a matching system preset you haven't installed in the Configuration Wizard
   is installed and selected when no installed preset fits, or when the installed ones are all
   another color and it fits better, or, from Prusa Connect, when it's closer to the product name
   (below).
5. **Nothing matches, or only other colors:** when the printer names a vendor and none of its
   presets of that material fits, or only ones in another color do (`Polymaker PETG Black` for a
   red Polymaker PETG), a user preset is created, named `<vendor> <material> <color>` (for
   example `Polymaker PETG Red`; a printer user color without a name goes by its hex, and a tool
   without a color gets no color in the name), copied from a preset of the same material that
   suits the extruder: the extruder's current one, else a Generic one, else Prusament. One saved
   from a variant comes last when the new name starts with the variant's: for a user color
   `Matt Olive`, a copy of `Spectrum PETG Matt Black` would make `Spectrum PETG Matt Olive` pass
   for a copy of `Spectrum PETG Matt`, never another color. Its vendor and filament color come
   from the printer. If that name is taken, it gets ` @INDX`. Tools with the same vendor, material
   and color share one new preset.
   It's saved with your other user presets; it isn't added to Filament DB. When no preset can be
   made (nothing suits the extruder to start from), the closest color is selected instead, and
   a note says why.

When the printer reports no vendor (the filament was loaded by G-code, or the vendor was
cleared), the current preset is kept if it's the right material, unless it's for abrasive
filament on a nozzle that isn't hardened; otherwise a Generic one is selected. A product name from
Prusa Connect without a brand orders the presets as below, but doesn't replace the current one or
install one: a word it shares with a preset (`Matte`) doesn't say whose filament it is. Nothing is
created without a vendor. A tool without a color doesn't count against a preset that names one.

### From Prusa Connect

A material assigned in Connect's Tools page comes from Connect's material database:

- **Vendor:** the material's brand, as the database spells it (`3D Fuel`, `eSUN`, `BambuLab`).
  These fold onto your presets' spellings like the printer's do.
- **Color:** the printer's color name that the product name contains (`Cyan` in `Pro PCTG,
  Translucent Cyan`, `Gray` for Grey), so presets named for a color match as they do with the
  printer. When the name has several (`PETG Black Silver Glitter`), the one closest to the
  product's color counts. The color code is the database's color for that product (`#0099E6`),
  which needn't be your preset's, and a preset named for the color matches whatever its own color
  code. Qualifiers are dropped: `Light Gray` is `Gray`. A product name without one of the
  printer's 16 color names goes by the color code alone: it has a name only when it's exactly one
  of the printer's colors.
- **Product:** the product name tells presets of one color apart, and the product lines of a
  vendor's system presets (`Fiberlogy Easy PLA` for `Easy PLA Navy Blue`). A preset is the closer
  the fewer words of the product name it lacks, and the fewer words of another product it has:
  for `Pro PCTG, Translucent Yellow`, `Pro PCTG Daffodil Yellow` lacks `Translucent` and has
  `Daffodil`, so `Pro PCTG Translucent Yellow` fits better. The plain `3D Fuel PCTG Yellow` that
  a sync creates lacks `Pro` and `Translucent`, so it's as close as `Pro PCTG Daffodil Yellow`.
  `High Speed` in `Prusament PLA High Speed Gravity
  Grey` is another product's for `PLA Gravity Grey`, as `Silk` is for `PLA White` and `HD` in
  `Fiberlogy HD PLA` for `Easy PLA Navy Blue`. `PLA+` reads as `PLA Plus`. Not counted: numbers
  (`2.0`, `40%`), words of no preset's name (a spool size), words after the color (`Tuned` in your
  copy of a preset) or after the name of the system preset a copy was saved from, and a nozzle
  part from its diameter on (`0.4 Tungsten Carbide HF`), so which of a product's presets suits
  the nozzle is still your choice.
  A closer preset with no word of another product replaces the current one. Between presets as
  close, or ones that each name another product (`Pro PCTG Fire Engine Red` and `Pro PCTG Toolbox
  Red` for `Pro PCTG, Iron Red`), the one you picked stays. A system preset that isn't installed
  is installed the same way, also over an installed one that fits the color less well (`3D-Fuel Pro PCTG Matte Black` over `3D-Fuel Pro PCTG` for
  `Pro PCTG, Matte Black`).
- **Fiber:** a product that names carbon or glass fiber (`XPETG CF`, `Pro PCTG-CF10`) prefers
  presets that name it too, by name or type, and one that doesn't prefers presets that don't.
  `Carbon` alone counts too, in a product name (`Kimya ABS Carbon Black`) and in an abrasive
  preset's name (`Extrudr GreenTEC Pro Carbon`); an abrasive preset that names no fiber fill
  counts as unfilled. This comes before the product name: a preset that agrees about fiber
  replaces the current one, or is installed, even when its name is farther from the product's.
- **Product colors:** a preset with every word of the product name isn't another color for the
  words after its material (`Gizmo Dorks HIPS MultiMaterial`), only for its own filament color,
  and a printer color that the product name has too (`Silver` in `PETG Black Silver Glitter`)
  isn't another color.
- A tool Connect has no assigned material for reports only the type and the printer's color, with
  no vendor. A color from the printer's palette gets the printer's name for it (`#00FFFF` is
  `Cyan`), as over PrusaLink; a user color has only its color code. Then it's treated like a tool
  without a vendor above. The preview's notes say what Prusa Connect, rather than the printer,
  reported.

### Empty tools

An empty tool keeps its extruder's preset. PrusaSlicer remembers which tools were empty, and which
docks weren't calibrated, at the last sync with the selected physical printer (or, from Prusa
Connect, for printer profiles of the same printer model while you stay signed in; Connect lists
every tool, so it never reports a dock as not calibrated):

- **The sidebar hides their filament lists**, as Bambu Studio and OrcaSlicer show only loaded
  filaments, and numbers the lists it still shows, so a gap (tools 1, 2, 3 and 5) stays clear.
  Untick **Hide empty extruders in the sidebar** in the preview to show them all; that choice is
  remembered. Every list shows again when you select another physical printer, or, after a sync
  from Prusa Connect, when you sign out, select a printer profile of another printer model, or
  select a PrusaLink physical printer. The extruders themselves stay: the INDX start G-code names
  all eight tools.
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
| *To read the filaments loaded in your printer, select a physical printer…* | Add and select a PrusaLink physical printer, or sign in to Prusa Account (section 1). If you're signed in, it says why Prusa Connect can't be read: the printer profile isn't a Prusa printer's with a printer model; select your printer's system profile, or one saved from it. |
| *… doesn't report the filaments in its tools* | The printer's firmware has no Filament Slots. |
| *… refused the API key or password* | Check them in the physical printer's settings, and that login is enabled on the printer. |
| *Couldn't reach the printer at …* | Check the address and that the printer is on the network. |
| *… reports no tools* | No dock is calibrated yet. |
| *None of the printers in your Prusa Connect account is a …* | The printer profile's printer model isn't one Connect lists for your printers: select the profile for your printer. |
| *… has an MMU* | Printers with an MMU aren't read from Prusa Connect yet. |
| *Prusa Connect didn't accept your Prusa Account sign-in* | PrusaSlicer renews the sign-in on its own: try again in a moment, or sign in again. |
| *Couldn't reach Prusa Connect* | Check your internet connection. |
| *Prusa Connect answered with an error* | Connect's own message follows, for example when your account has no access to the printer list. |
| *Prusa Connect has no tools of … yet* | The printer hasn't reported its tools to Connect: turn it on and connect it, then try again. |

## 7. How it works

The printer serves `GET /api/v1/tools` over PrusaLink (Digest or API key, like uploads):

```json
{"tools":{"1":{"loaded":true,"material":"PLA","vendor":"Prusament","color":"Orange","color_hex":"#F8651B",
"nozzle_diameter":0.40,"hardened":false,"high_flow":false}, ...}}
```

Prusa Connect serves `GET /slicer/v2/printers` with the Prusa Account access token (`Authorization:
Bearer`), the same list PrusaSlicer 3.0 reads. Its tools count from `"0"`, and a material assigned
in Connect carries its brand, product name and color:

```json
{"printers":[{"uuid":"…","name":"Core One INDX","legacy_printer_model":"COREONE_INDX8T",
"tools":{"0":{"features":{"nozzle_diameter":0.25,"nozzle_high_flow":false,"nozzle_hardened":false},
"material_package_instance":{"package":{"material":{"type":"PCTG","name":"Pro PCTG, Translucent Cyan",
"brand":{"name":"3D Fuel"},"primary_color":{"color_rgba":"#0099e6ff"}}}}},
"4":{"features":{…},"material_package_instance":null}, ...}}]}
```

- `src/slic3r/Utils/PrinterTools.{hpp,cpp}`: fetching and parsing (`fetch_printer_tools`,
  `PrintHost::get_tools`, `fetch_connect_printers`).
- `src/slic3r/Utils/ConnectPrinterTools.cpp`: parsing Connect's printer list
  (`parse_connect_printers_json`, `connect_printer_fits`), with only the JSON library.
- `src/slic3r/Utils/FilamentSync.{hpp,cpp}`: matching (`make_plan`) and applying
  (`apply_presets`), with no wx dependency.
- `src/slic3r/GUI/FilamentSyncDialog.{hpp,cpp}`: choosing the source (`filament_sync_source`), the
  sidebar button, the menu item, the Connect printer picker and the preview.

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
