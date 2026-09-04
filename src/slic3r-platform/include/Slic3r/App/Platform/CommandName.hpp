#pragma once

namespace Slic3r::App::Platform {
struct CommandName
{
    static constexpr const char* AddObject       = "add-object";
    static constexpr const char* AddVolume       = "add-volume";
    static constexpr const char* AddInstance     = "add-instance";
    static constexpr const char* DelInstance     = "del-instance";
    static constexpr const char* DeleteSelected  = "delete-selected";
    static constexpr const char* CopyModelItems  = "copy-selected";
    static constexpr const char* PasteModelItems = "paste-model-items";

    static constexpr const char* NewProject    = "new-project";
    static constexpr const char* OpenProject   = "open-project";
    static constexpr const char* SaveProject   = "save-project";
    static constexpr const char* SaveProjectAs = "save-project-as";

    static constexpr const char* SelectAll      = "select-all";
    static constexpr const char* ClearSelection = "clear-selection";

    static constexpr const char* DeactivateCurrentGizmo = "deactivate-current-gizmo";

    static constexpr const char* MoveGizmo                  = "move-gizmo";
    static constexpr const char* RotateGizmo                = "rotate-gizmo";
    static constexpr const char* ScaleGizmo                 = "scale-gizmo";
    static constexpr const char* PlaceOnFace                = "place-on-face-gizmo";
    static constexpr const char* ArrangeGizmo               = "arrange-gizmo";
    static constexpr const char* Arrange                    = "arrange";
    static constexpr const char* ArrangeSelection           = "arrange-selection";
    static constexpr const char* SimplifyGizmo              = "simplify-gizmo";
    static constexpr const char* TextGizmo                  = "text-gizmo";
    static constexpr const char* CreateObjectAsText         = "create-object-as-text";
    static constexpr const char* SvgGizmo                   = "svg-gizmo";
    static constexpr const char* CutGizmo                   = "cut-gizmo";
    static constexpr const char* MeasureGizmo               = "measure-gizmo";
    static constexpr const char* PaintOnSupportsGizmo       = "paint-on-supports-gizmo";
    static constexpr const char* PaintOnSeamsGizmo          = "paint-on-seams-gizmo";
    static constexpr const char* PaintOnFuzzySkinGizmo      = "paint-on-fuzzy-skin-gizmo";
    static constexpr const char* MultiMaterialPaintingGizmo = "multi-material-painting-gizmo";
    static constexpr const char* VariableLayerHeightGizmo   = "variable-layer-height-gizmo";
    static constexpr const char* HeightRangeGizmo           = "height-range-gizmo";

    static constexpr const char* SwitchToPlater  = "switch-to-plater";
    static constexpr const char* SwitchToPreview = "switch-to-preview";
    static constexpr const char* SwitchToInspect = "switch-to-inspect";

    static constexpr const char* ShowTravels         = "show-travels";
    static constexpr const char* ShowWipes           = "show-wipes";
    static constexpr const char* ShowRetractions     = "show-retractions";
    static constexpr const char* ShowUnretractions   = "show-unretractions";
    static constexpr const char* ShowSeams           = "show-seams";
    static constexpr const char* ShowToolChanges     = "show-tool-changes";
    static constexpr const char* ShowColorChanges    = "show-color-changes";
    static constexpr const char* ShowPausePrints     = "show-pause-prints";
    static constexpr const char* ShowCustomGCodes    = "show-custom-g-codes";
    static constexpr const char* ShowCenterOfGravity = "show-center-of-gravity";
    static constexpr const char* ShowToolMarker      = "show-tool-marker";
    static constexpr const char* ShowShell           = "show-tool-shell";

    static constexpr const char* Search       = "search";
    static constexpr const char* Preferences  = "preferences";
    static constexpr const char* ShapeGallery = "shape-gallery";
    static constexpr const char* ShowLabel    = "show-label";
    static constexpr const char* FullScreen   = "full-screen";
    static constexpr const char* Exit         = "exit";

    static constexpr const char* Undo  = "undo";
    static constexpr const char* Redo  = "redo";
    static constexpr const char* Copy  = "copy";
    static constexpr const char* Paste = "paste";

    static constexpr const char* ZoomIn                 = "zoom-in";
    static constexpr const char* ZoomOut                = "zoom-out";
    static constexpr const char* CameraProjectionSwitch = "camera-projection-switch";
    static constexpr const char* LookAtActiveBed        = "look-at-active-bed";
    static constexpr const char* CameraDefaultView      = "camera-default-view";
    static constexpr const char* CameraTopView          = "camera-top-view";
    static constexpr const char* CameraBottomView       = "camera-bottom-view";
    static constexpr const char* CameraFrontView        = "camera-front-view";
    static constexpr const char* CameraRearView         = "camera-rear-view";
    static constexpr const char* CameraLeftView         = "camera-left-view";
    static constexpr const char* CameraRightView        = "camera-right-view";

    static constexpr const char* ExportAsStl          = "export-as-stl";
    static constexpr const char* ReplaceWithStl       = "replace-with-stl";
    static constexpr const char* ReloadFromDisk       = "reload-from-disk";
    static constexpr const char* ConfigurationWizard  = "configuration-wizard";
    static constexpr const char* CheckConfigUpdates   = "check-config-updates";
    static constexpr const char* CheckAppUpdates      = "check-app-updates";
    static constexpr const char* FlashPrinterFirmware = "flash-printer-firmware";

    static constexpr const char* ImportGeometry = "import-geometry";

    static constexpr const char* ExportGcode        = "export-gcode";
    static constexpr const char* SendGcode          = "send-gcode";
    static constexpr const char* ExportGcodeToFlash = "export-gcode-to-flash";

    static constexpr const char* OnlinePresetUpdate = "online-preset-update";
    static constexpr const char* PresetReposManagement = "preset-repos-management";

    static constexpr const char* PSWebsite      = "ps-website";
    static constexpr const char* Samples        = "samples";
    static constexpr const char* Releases       = "releases";
    static constexpr const char* SystemInfo     = "system-info";
    static constexpr const char* ConfigFolder   = "config-folder";

    static constexpr const char* ReportAnIssue           = "report-issue";
    static constexpr const char* About                   = "about";
    static constexpr const char* TipOfTheDay             = "tip-of-the-day";
    static constexpr const char* KeyboardShortcutsDialog = "keyboard-shortcuts";

    static constexpr const char* ArrangeBed          = "arrange-bed";
    static constexpr const char* ArrangeSelectionBed = "arrange-selection-bed";
    static constexpr const char* SelectAllOnBed      = "select-all-on-bed";
    static constexpr const char* DeleteBed           = "delete-bed";

    static constexpr const char* SetNumberOfInstances   = "set-number-of-instances";
    static constexpr const char* FixWithRepairAlgorithm = "fix-with-repair-algorithm";
    static constexpr const char* SetAsPrintable         = "set-as-printable";
    static constexpr const char* SplitToVolumes         = "split-to-volumes";

    static constexpr const char* PluginExecutePrefix = "plugin-execute-";
    static constexpr const char* PluginRescan = "plugin-rescan";
    static constexpr const char* PluginFolder = "plugin-folder";
    static constexpr const char* PluginInstall = "plugin-install";
};
} // namespace Slic3r::App::Platform
