#pragma once

#include <string>
#include <variant>

namespace Slic3r::App {

enum class MenuItemName
{
    Separator,
    MainMenu,

    Edit,
    SelectAll,
    DeselectAll,
    DeleteSelected,
    Undo,
    Redo,
    Copy,
    Paste,
    ReloadFromDisk,
    Search,
    Arrange,
    ArrangeSelection,

    ShapeGallery,

    View,
    ShowLabel,
    FullScreen,
    ZoomIn,
    ZoomOut,
    CameraProjectionSwitch,
    LookAtActiveBed,
    CameraDefaultView,
    CameraTopView,
    CameraBottomView,
    CameraFrontView,
    CameraRearView,
    CameraLeftView,
    CameraRightView,

    Preferences,

    Configuration,
    ConfigurationWizard,
    CheckConfigUpdates,
    CheckAppUpdates,
    FlashPrinterFirmware,

    Help,
    PSWebsite,
    Samples,
    Releases,
    SystemInfo,
    ConfigFolder,
    ReportAnIssue,
    About,
    TipOfTheDay,
    KeyboardShortcutsDialog,

    Exit,

    FileMenu,
    NewProject,
    OpenProject,
    RecentProjects,
    SaveProject,
    SaveProjectAs,

    Import,
    ImportGeometry,

    Export,
    ExportGcode,
    SendGcode,
    ExportGcodeToFlash,

    OnlinePresetUpdate,
    PresetReposManagement,

    JumpToValue,

    BedContextMenu,
    AddObjectShape,
    ObjectShapeLoad,
    ObjectShapeCube,
    ObjectShapeCylinder,
    ObjectShapeSphere,
    ObjectShapeText,
    ObjectShapeSvg,
    ObjectShapeFromGallery,
    ArrangeBed,
    ArrangeSelectionBed,
    SelectAllOnBed,
    DeleteBed,

    ObjectContextMenu,
    CopyObject,
    PasteObject,
    DeleteSelectedObject,
    SetNumberOfInstances,
    FillBedWithInstances,
    ExportObject,
    ReplaceObject,
    ReloadObject,
    SplitObject,
    SplitObjectToObjects,
    SplitObjectToVolumes,
    ScaleToPrintVolume,
    FixObjectWithRepairAlgorithm,
    AddVolume,
    SolidPartVolume,
    NegativeVolume,
    ModifierVolume,
    SupportBlocker,
    SupportModifier,
    SolidPartVolumeShapeLoad,
    SolidPartVolumeShapeCube,
    SolidPartVolumeShapeCylinder,
    SolidPartVolumeShapeSphere,
    SolidPartVolumeShapeText,
    SolidPartVolumeShapeSVG,
    SolidPartVolumeShapeFromGallery,
    NegativeVolumeShapeLoad,
    NegativeVolumeShapeCube,
    NegativeVolumeShapeCylinder,
    NegativeVolumeShapeSphere,
    NegativeVolumeShapeText,
    NegativeVolumeShapeSVG,
    NegativeVolumeShapeFromGallery,
    ModifierVolumeShapeLoad,
    ModifierVolumeShapeCube,
    ModifierVolumeShapeCylinder,
    ModifierVolumeShapeSphere,
    ModifierVolumeShapeSVG,
    ModifierVolumeShapeText,
    ModifierVolumeShapeFromGallery,
    SupportBlockerShapeLoad,
    SupportBlockerShapeCube,
    SupportBlockerShapeCylinder,
    SupportBlockerShapeSphere,
    SupportBlockerShapeFromGallery,
    SupportModifierShapeLoad,
    SupportModifierShapeCube,
    SupportModifierShapeCylinder,
    SupportModifierShapeSphere,
    SupportModifierShapeFromGallery,
    InvalidateCutInfo,
    SetAsSeparateObject,
    PrintableObject,

    MultiObjectsContextMenu,
    CopyMultiObjects,
    DeleteSelectedMultiObjects,
    SetMultiObjectsNumberOfInstances,
    MergeMultiObjects,
    FixMultiObjectWithRepairAlgorithm,
    PrintableMultiObjects,

    SvgOrTextContextMenu,
    EditSvgOrText,
    DeleteSelectedSvgOrText,

    VolumeContextMenu,
    CopyVolume,
    PasteVolume,
    DeleteSelectedVolume,
    ExportVolume,
    ReplaceVolume,
    ReloadVolume,
    SplitVolume,
    FixVolumeWithRepairAlgorithm,

    Plugins,
    PluginRescan,
    PluginFolder,
    PluginInstall,
};

class UniversalMenuItemName: public std::variant<MenuItemName, std::string>
{
    using Base = std::variant<MenuItemName, std::string>;
public:
    using Base::variant;

    bool operator==(const UniversalMenuItemName& other) const
    {
        return static_cast<const Base&>(*this) == static_cast<const Base&>(other);
    }

    bool matches(MenuItemName name) const
    {
        return std::holds_alternative<MenuItemName>(*this) && std::get<MenuItemName>(*this) == name;
    }
};



} // namespace Slic3r::App

template <>
struct std::hash<Slic3r::App::UniversalMenuItemName>
{
    std::size_t operator()(const Slic3r::App::UniversalMenuItemName& item) const noexcept
    {
        using Base = std::variant<Slic3r::App::MenuItemName, std::string>;
        return std::hash<Base>{}(static_cast<const Base&>(item));
    }
};