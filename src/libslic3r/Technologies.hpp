///|/ Copyright (c) Prusa Research 2018 - 2023 Vojtěch Bubník @bubnikv, Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka, Filip Sykala @Jony01, Lukáš Matěna @lukasmatena, David Kocík @kocikdav, Vojtěch Král @vojtechkral
///|/ Copyright (c) BambuStudio 2023 manch1n @manch1n
///|/ Copyright (c) 2019 BeldrothTheGold @BeldrothTheGold
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef _prusaslicer_technologies_h_
#define _prusaslicer_technologies_h_

//=============
// debug techs
//=============
// Shows camera target in the 3D scene
#define ENABLE_SHOW_CAMERA_TARGET 0
// Log debug messages to console when changing selection
#define ENABLE_SELECTION_DEBUG_OUTPUT 0
// Renders a small sphere in the center of the bounding box of the current selection when no gizmo is active
#define ENABLE_RENDER_SELECTION_CENTER 0
// Shows an imgui dialog with camera related data
#define ENABLE_CAMERA_STATISTICS 0
// Enable extracting thumbnails from selected gcode and save them as png files
#define ENABLE_THUMBNAIL_GENERATOR_DEBUG 0
// Disable synchronization of unselected instances
#define DISABLE_INSTANCES_SYNCH 0
// Use wxDataViewRender instead of wxDataViewCustomRenderer
#define ENABLE_NONCUSTOM_DATA_VIEW_RENDERING 0
// Enable project dirty state manager debug window
#define ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW 0
// Disable using instanced models to render options in gcode preview
#define DISABLE_GCODEVIEWER_INSTANCED_MODELS 1
// Enable Measure Gizmo debug window
#define ENABLE_MEASURE_GIZMO_DEBUG 0
// Enable scene raycast picking debug window
#define ENABLE_RAYCAST_PICKING_DEBUG 0
// Shows an imgui dialog with GLModel statistics data
#define ENABLE_GLMODEL_STATISTICS 0
// Shows an imgui dialog containing the matrices of the selected volumes
#define ENABLE_MATRICES_DEBUG 0
// Shows an imgui dialog containing data from class ObjectManipulation
#define ENABLE_OBJECT_MANIPULATION_DEBUG 0
// Shows an imgui dialog containing data for class GLCanvas3D::SLAView
#define ENABLE_SLA_VIEW_DEBUG_WINDOW 0


// Enable rendering of objects using environment map
#define ENABLE_ENVIRONMENT_MAP 0
// Enable smoothing of objects normals
#define ENABLE_SMOOTH_NORMALS 0

// Enable imgui dialog which allows to set the parameters used to export binarized gcode
#define ENABLE_BINARIZED_GCODE_DEBUG_WINDOW 0

// Enable imgui debug dialog for new gcode viewer (using libvgcode)
#define ENABLE_NEW_GCODE_VIEWER_DEBUG 0
// Enable extension of tool position imgui dialog to show actual speed profile
#define ENABLE_ACTUAL_SPEED_DEBUG 1

// This technology enables a hack which resolves the slow down on MAC when running the application as GCodeViewer.
// For yet unknow reason the slow down disappears if any of the toolbars is renderered.
// This hack keeps the collapse toolbar enabled and renders it outside of the screen.
#define ENABLE_HACK_GCODEVIEWER_SLOW_ON_MAC 1

#endif // _prusaslicer_technologies_h_
