#pragma once

#include <variant>

namespace Slic3r::Biz {

enum class UndoSnapshotType
{
    None,
    InitializeProject,
    QuickDrag,
    QuickDragAndAddBed,
    Translate,
    SetTranslation,
    Rotate,
    SetRotation,
    RevertRotation,
    Scale,
    SetScale,
    RevertScale,
    PlaceOnFace,
    Arrange,
    Cut,
    CutPlaneMove,
    CutChangeMode,
    CutAddConnector,
    CutRemoveConnector,
    CutMoveConnector,
    CutResetConnectors,
    CutFlipPlane,
    CutChangeConnectorType,
    CutChangeConnectorStyle,
    CutChangeConnectorShape,
    CutChangeMainSettings,
    AddObject,
    AddInstance,
    DelInstance,
    SetNumberOfInstances,
    AddVolume,
    AddVolumeCube,
    AddVolumeCylinder,
    AddVolumeSphere,
    AddVolumeText,
    AddVolumeSvg,
    AddVolumeGallery,
    AddNegativeVolume,
    AddNegativeVolumeCube,
    AddNegativeVolumeCylinder,
    AddNegativeVolumeSphere,
    AddNegativeVolumeText,
    AddNegativeVolumeSvg,
    AddNegativeVolumeGallery,
    AddModifier,
    AddModifierCube,
    AddModifierCylinder,
    AddModifierSphere,
    AddModifierText,
    AddModifierSvg,
    AddModifierGallery,
    AddSupportBlocker,
    AddSupportBlockerCube,
    AddSupportBlockerCylinder,
    AddSupportBlockerSphere,
    AddSupportBlockerGallery,
    AddSupportModifier,
    AddSupportModifierCube,
    AddSupportModifierCylinder,
    AddSupportModifierSphere,
    AddSupportModifierGallery,
    AddCube,
    AddCylinder,
    AddSphere,
    DeleteSelection,
    SetAsSeparateObject,
    SplitToObjects,
    SplitToVolumes,
    MergeToOneObject,
    RepairObjectMesh,
    InvalidateCutInfo,
    SetAsPrintable,
    ChangeVolumeType,
    PaintOnSupportsStroke,
    PaintOnSupportsAutomaticPainting,
    PaintOnSeamsStroke,
    PaintOnFuzzySkinStroke,
    MMPaintingStroke,
    ActivateGizmo,
    DeactivateGizmo,
    SelectBed,
    AddConfigContainer,
    DuplicateConfigContainer,
    DeleteConfigContainer,
    AddBed,
    DeleteBed,
    SelectPrinterPreset,
    SetPartSettingsValue,
    PasteObjects,
    PasteVolumes,
    VariableLayerHeightStroke,
    VariableLayerHeightSmooth,
    VariableLayerHeightReset,
    VariableLayerHeightAdaptive,
    HeightRangeAdd,
    HeightRangeDelete,
    HeightRangeValueChange,
    HeightRangeOverrideChange,
    HeightRangeRestart,
    HeightRangePaste,
    HeightRangeLayerHeightOverride,
    ExecutePlugin,
    ReplaceWithStl,
    ReloadFromDisk,
    EditVirtualExtruders,
};

namespace UndoSnapshotSelection {
struct Next
{};

struct Prev
{};

using Variant = std::variant<std::size_t, Next, Prev>;
} // namespace UndoSnapshotSelection

class IUndoProvider
{
public:
    virtual ~IUndoProvider() = default;

    virtual void take_snapshot(UndoSnapshotType type) = 0;

    virtual void select_snapshot(UndoSnapshotSelection::Variant snapshot_variant) = 0;

    virtual bool is_undo_possible() const = 0;

    virtual bool is_redo_possible() const = 0;
};

} // namespace Slic3r::Biz
