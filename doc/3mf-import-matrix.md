# 3MF import matrix

Every way a model can reach the slicer, and what happens in each case. Everything here is visible on
screen — open the case you want to check and compare.

What ends up in the slicer:

| | |
|---|---|
| **project** | Opens as a project — shapes and settings |
| **shapes only** | Any 3MF settings are dropped |
| **nothing** | Nothing is placed on the plate |

---

## 1. Printables link, slicer closed

The slicer starts because of the click. No question is asked in any of these cases.

| Slicer state | What is loaded | How it is loaded | What appears in the slicer | Notifications and dialogs |
|---|---|---|---|---|
| Slicer closed | One STL, OBJ or STEP model | Click Open in PrusaSlicer on printables.com in your web browser | **shapes only** — the slicer starts and the model is placed in the project it opens with. No second tab is created. | A download progress notification. Then Download Finished with Printables and Open Folder buttons, which disappears on its own after 20 seconds. |
| Slicer closed | One 3MF saved by PrusaSlicer | Click Open in PrusaSlicer on printables.com in your web browser | **project** — the slicer starts and the model takes over the empty project it opened with, bringing the print, filament and printer settings from the file. No second tab is created. | A download progress notification. Then Download Finished with Printables and Open Folder buttons, gone after 20 seconds. No settings notification — nothing was dropped. |
| Slicer closed | One 3MF from Bambu Studio, Orca or a modelling tool | Click Open in PrusaSlicer on printables.com in your web browser | **shapes only** — the slicer starts and the model takes over the empty project it opened with, using the presets that were selected before. Only the shapes come from the file. No second tab is created. | Dialog to confirm: "The 3MF file does not contain PrusaSlicer configuration. Only geometry was loaded." Then Download Finished with Printables and Open Folder buttons. |

---

## 2. Printables tab inside the slicer

Continue adds to the project you have open, whether it is empty or full. Create new project uses
the open project when it is empty, and adds a tab when it already holds models.

| Slicer state | What is loaded | How it is loaded | What appears in the slicer | Notifications and dialogs |
|---|---|---|---|---|
| Slicer running | One model, any file type | Press Download on a model in the Printables tab | **nothing yet** — the file is saved to your downloads folder. Nothing is placed on the plate until you answer the notification. | A download progress notification. Then Download Finished with Load and Load as New Project buttons. It stays on screen until you click one of them or close it. What each button does is in section 4. |
| Slicer running | A print list with any number of models | Press Add to print list on each model, then press Continue in the print list | **shapes only** — every model is added to the project you have open. A model listed with quantity 3 is placed three times. Settings of any PrusaSlicer 3MF in the list are ignored. | One download notification for the whole list, counting the files. "Imported geometry only." appears once if at least one file in the list was a 3MF, whether or not it carried PrusaSlicer settings. |
| Slicer running | A print list with two or more models | Press Add to print list on each model, then press Create new project in the print list | **shapes only** — every model is added as shapes to a project of its own — the open project if it is empty, otherwise a new tab. Settings of any PrusaSlicer 3MF in the list are dropped; a list of several models never opens as a project. | One download notification for the whole list, counting the files. "Imported geometry only." once if at least one file was a 3MF, whether or not it carried PrusaSlicer settings. |
| Slicer running | A print list holding exactly one 3MF saved by PrusaSlicer | Press Add to print list on the model, then press Create new project in the print list | **project** — the model opens as a project with the print, filament and printer settings from the file, in the open project if it is empty, otherwise in a new tab. | The download notification only. No settings notification — nothing was dropped. |
| Slicer running | A print list holding exactly one 3MF from Bambu Studio or Orca | Press Add to print list on the model, then press Create new project in the print list | **shapes only** — the model opens in a project of its own, using the presets that were selected before — the open project if it is empty, otherwise a new tab. Only the shapes come from the file. | Dialog to confirm: "The 3MF file does not contain PrusaSlicer configuration. Only geometry was loaded." Plus the download notification. |
| Slicer running | A print list holding exactly one STL | Press Add to print list on the model, then press Create new project in the print list | **shapes only** — the model is added to a project of its own — the open project if it is empty, otherwise a new tab. | The download notification only. |

---

## 3. Printables link, slicer already running

The click reaches the slicer that is already open. An empty project counts as no project: the model
opens straight away, exactly as if the slicer had been closed. Only a project that already holds
models is protected by the question.

| Slicer state | What is loaded | How it is loaded | What appears in the slicer | Notifications and dialogs |
|---|---|---|---|---|
| Slicer running, open project empty | One STL, OBJ or STEP model | Click Open in PrusaSlicer on printables.com in your web browser | **shapes only** — the slicer window comes to the front and the model is placed in the empty project you already had open. No extra tab is created. | A download progress notification. Then Download Finished with Printables and Open Folder buttons, gone after 20 seconds. No question is asked. |
| Slicer running, open project empty | One 3MF saved by PrusaSlicer | Click Open in PrusaSlicer on printables.com in your web browser | **project** — the slicer window comes to the front and the model takes over the empty project you already had open, bringing the print, filament and printer settings from the file. No extra tab is created. | A download progress notification. Then Download Finished with Printables and Open Folder buttons, gone after 20 seconds. No question, and no settings notification — nothing was dropped. |
| Slicer running, open project empty | One 3MF from Bambu Studio, Orca or a modelling tool | Click Open in PrusaSlicer on printables.com in your web browser | **shapes only** — the slicer window comes to the front and the model takes over the empty project you already had open, using the presets that were selected before. No extra tab is created. Only the shapes come from the file. | Dialog to confirm: "The 3MF file does not contain PrusaSlicer configuration. Only geometry was loaded." Then Download Finished with Printables and Open Folder buttons. No question is asked. |
| Slicer running, one or more projects with models | One model, any file type | Click Open in PrusaSlicer on printables.com in your web browser | **nothing yet** — the slicer window comes to the front. Nothing is placed on the plate until you answer the notification. | Download Finished with Load and Load as New Project buttons, which stays until you click one of them or close it. |

---

## 4. Answering the Load / Load as New Project notification

Reached from the Download button in the Printables tab, and from a Printables link clicked while the
slicer runs.

| Slicer state | What is loaded | How it is loaded | What appears in the slicer | Notifications and dialogs |
|---|---|---|---|---|
| Slicer running, notification on screen | One STL, OBJ or STEP model | Click Load on the Download Finished notification | **shapes only** — the model is added to the project you have open. | The notification closes. Nothing else appears. |
| Slicer running, notification on screen | One 3MF saved by PrusaSlicer | Click Load on the Download Finished notification | **shapes only** — the shapes are added to the project you have open. Its print, filament and printer settings are ignored so your project keeps its own. | "Imported geometry only. 3MF settings ignored to preserve your project." for 10 seconds. |
| Slicer running, notification on screen | One 3MF from Bambu Studio, Orca or a modelling tool | Click Load on the Download Finished notification | **shapes only** — the shapes are added to the project you have open. | "Imported geometry only. 3MF settings ignored to preserve your project." for 10 seconds, even though the file had no settings to drop. No dialog appears on this route. |
| Slicer running, notification on screen | One 3MF saved by PrusaSlicer | Click Load as New Project on the Download Finished notification | **project** — a new project tab opens with the shapes and with the print, filament and printer settings from the file. | Nothing. |
| Slicer running, notification on screen | One 3MF from Bambu Studio, Orca or a modelling tool | Click Load as New Project on the Download Finished notification | **shapes only** — a new project tab opens with the presets that were selected before. Only the shapes come from the file. | Dialog to confirm: "The 3MF file does not contain PrusaSlicer configuration. Only geometry was loaded." |
| Slicer running, notification on screen | One STL, OBJ or STEP model | Click Load as New Project on the Download Finished notification | **shapes only** — a new empty project tab opens and the model is added to it. | The notification closes. Nothing else appears. |

---

## 5. Files from your own disk

The same 3MF behaves differently depending on how you bring it in — worth testing side by side.

| Slicer state | What is loaded | How it is loaded | What appears in the slicer | Notifications and dialogs |
|---|---|---|---|---|
| Slicer running | One 3MF saved by PrusaSlicer | File ▸ Open Project | **project** — a new project tab opens with the shapes and with the print, filament and printer settings from the file. | No settings notification. If the file comes from PrusaSlicer 2.x: dialog "Loading legacy 3MF projects is not supported yet." If it was painted in a newer PrusaSlicer, a dialog lists what could not be read. |
| Slicer running | One 3MF from Bambu Studio, Orca or a modelling tool | File ▸ Open Project | **shapes only** — a new project tab opens with the presets that were selected before. Only the shapes come from the file. | Dialog to confirm: "The 3MF file does not contain PrusaSlicer configuration. Only geometry was loaded." |
| Slicer running | One or several files — STL, OBJ, STEP, SVG or 3MF | File ▸ Import | **shapes only** — everything is added to the project you have open and arranged on the plate. | "Imported geometry only." for 10 seconds if at least one file was a 3MF, whether or not it carried PrusaSlicer settings. Several separate meshes on a multi-material printer: a question asking whether they are one object with several parts. A file that cannot be read: an import error dialog listing it. |
| Slicer running | Exactly one 3MF saved by PrusaSlicer | Drag the file onto the slicer window | **project** — a new project tab opens with the settings from the file; the same file taken through File ▸ Import loses them instead. | No settings notification. The same dialogs as File ▸ Open Project can appear. |
| Slicer running | Two or more files, or any single file that is not a 3MF | Drag the files onto the slicer window | **shapes only** — everything is added to the project you have open. | "Imported geometry only." for 10 seconds if at least one file was a 3MF, whether or not it carried PrusaSlicer settings. |

---

## Worth knowing before testing

- **Which 3MFs have settings.** A 3MF saved by PrusaSlicer carries print, filament and printer
  settings. One exported by Bambu Studio, Orca or a modelling tool does not. The confirm dialog about
  missing configuration appears only when such a file is opened as a project.
- **The settings notification** reads "Imported geometry only. 3MF settings ignored to preserve your
  project." and appears whenever any 3MF is added to an open project as shapes, whether or not the
  file carried settings. It sits in the bottom-right corner for 10 seconds and closes the moment you
  load anything else, so it can never look like it describes the newer file.
- **An empty project counts as no project.** A Printables link that arrives while the open project is
  empty behaves as if the slicer had been closed — no question. Only a project that already holds
  models gets the Load / Load as New Project question. The Download button in the Printables tab
  still asks either way.
- **"A new project" uses the empty one you already have.** When a model opens as its own project and
  the project on screen is still empty, that project is used instead of adding a tab — whether the
  slicer just started for the link or was already open.
- **Czech wording** is not in the build yet: "Přidána pouze geometrie. Nastavení z 3MF bylo
  ignorováno pro zachování vašeho projektu."
