#/|/ Copyright (c) Prusa Research 2018 - 2023 Tomáš Mészáros @tamasmeszaros, David Kocík @kocikdav, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Filip Sykala @Jony01, Oleksandra Iushchenko @YuSanka, Lukáš Hejl @hejllukas, Vojtěch Král @vojtechkral
#/|/ Copyright (c) 2023 Pedro Lamas @PedroLamas
#/|/ Copyright (c) 2020 Sergey Kovalev @RandoMan70
#/|/ Copyright (c) 2021 Boleslaw Ciesielski
#/|/ Copyright (c) 2019 Spencer Owen @spuder
#/|/ Copyright (c) 2019 Stephan Reichhelm @stephanr
#/|/
#/|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
#/|/
cmake_minimum_required(VERSION 3.13)
project(libslic3r_gui)

include(PrecompiledHeader)

set(SLIC3R_GUI_SOURCES
    pchheader.cpp
    pchheader.hpp
    GUI/AboutDialog.cpp
    GUI/AboutDialog.hpp
    GUI/ArrangeSettingsDialogImgui.hpp
    GUI/ArrangeSettingsDialogImgui.cpp
    GUI/SysInfoDialog.cpp
    GUI/SysInfoDialog.hpp
    GUI/KBShortcutsDialog.cpp
    GUI/KBShortcutsDialog.hpp
    GUI/BackgroundSlicingProcess.cpp
    GUI/BackgroundSlicingProcess.hpp
    GUI/BitmapCache.cpp
    GUI/BitmapCache.hpp
    GUI/ConfigSnapshotDialog.cpp
    GUI/ConfigSnapshotDialog.hpp
    GUI/3DScene.cpp
    GUI/3DScene.hpp
    GUI/Accessibility.cpp
    GUI/format.hpp
    GUI/GLShadersManager.hpp
    GUI/GLShadersManager.cpp
    GUI/GLShader.cpp
    GUI/GLShader.hpp    
    GUI/GLCanvas3D.hpp
    GUI/GLCanvas3D.cpp
    GUI/SceneRaycaster.hpp
    GUI/SceneRaycaster.cpp	
    GUI/OpenGLManager.hpp
    GUI/OpenGLManager.cpp
    GUI/Selection.hpp
    GUI/Selection.cpp    
    GUI/Gizmos/GLGizmosManager.cpp
    GUI/Gizmos/GLGizmosManager.hpp
    GUI/Gizmos/GLGizmosCommon.cpp
    GUI/Gizmos/GLGizmosCommon.hpp
    GUI/Gizmos/GLGizmoBase.cpp
    GUI/Gizmos/GLGizmoBase.hpp
    GUI/Gizmos/GLGizmoSlaBase.cpp
    GUI/Gizmos/GLGizmoSlaBase.hpp	
    GUI/Gizmos/GLGizmoEmboss.cpp
    GUI/Gizmos/GLGizmoEmboss.hpp
    GUI/Gizmos/GLGizmoMove.cpp
    GUI/Gizmos/GLGizmoMove.hpp
    GUI/Gizmos/GLGizmoRotate.cpp
    GUI/Gizmos/GLGizmoRotate.hpp
    GUI/Gizmos/GLGizmoScale.cpp
    GUI/Gizmos/GLGizmoScale.hpp
    GUI/Gizmos/GLGizmoSlaSupports.cpp
    GUI/Gizmos/GLGizmoSlaSupports.hpp
    GUI/Gizmos/GLGizmoFdmSupports.cpp
    GUI/Gizmos/GLGizmoFdmSupports.hpp
    GUI/Gizmos/GLGizmoFlatten.cpp
    GUI/Gizmos/GLGizmoFlatten.hpp
    GUI/Gizmos/GLGizmoCut.cpp
    GUI/Gizmos/GLGizmoCut.hpp
    GUI/Gizmos/GLGizmoHollow.cpp
    GUI/Gizmos/GLGizmoHollow.hpp
    GUI/Gizmos/GLGizmoPainterBase.cpp
    GUI/Gizmos/GLGizmoPainterBase.hpp
    GUI/Gizmos/GLGizmoSeam.cpp
    GUI/Gizmos/GLGizmoSeam.hpp
    GUI/Gizmos/GLGizmoSimplify.cpp
    GUI/Gizmos/GLGizmoSimplify.hpp
    GUI/Gizmos/GLGizmoSVG.cpp
    GUI/Gizmos/GLGizmoSVG.hpp
    GUI/Gizmos/GLGizmoMmuSegmentation.cpp
    GUI/Gizmos/GLGizmoMmuSegmentation.hpp
    GUI/Gizmos/GLGizmoMeasure.cpp
    GUI/Gizmos/GLGizmoMeasure.hpp
    GUI/GLSelectionRectangle.cpp
    GUI/GLSelectionRectangle.hpp
    GUI/GLModel.hpp
    GUI/GLModel.cpp
    GUI/GLTexture.hpp
    GUI/GLTexture.cpp
    GUI/GLToolbar.hpp
    GUI/GLToolbar.cpp
    GUI/GCodeViewer.hpp
    GUI/GCodeViewer.cpp    
    GUI/Preferences.cpp
    GUI/Preferences.hpp
    GUI/PresetHints.cpp
    GUI/PresetHints.hpp
    GUI/GUI.cpp
    GUI/GUI.hpp
    GUI/GUI_Init.cpp
    GUI/GUI_Init.hpp
    GUI/GUI_Preview.cpp
    GUI/GUI_Preview.hpp
    GUI/GUI_App.cpp
    GUI/GUI_App.hpp
    GUI/GUI_Utils.cpp
    GUI/GUI_Utils.hpp
    GUI/GUI_Geometry.cpp
    GUI/GUI_Geometry.hpp
    GUI/I18N.cpp
    GUI/I18N.hpp
    GUI/IconManager.cpp
    GUI/IconManager.hpp
    GUI/MainFrame.cpp
    GUI/MainFrame.hpp
    GUI/Plater.cpp
    GUI/Plater.hpp
    GUI/PresetComboBoxes.hpp
    GUI/PresetComboBoxes.cpp
    GUI/BitmapComboBox.hpp
    GUI/BitmapComboBox.cpp
    GUI/EditGCodeDialog.hpp
    GUI/EditGCodeDialog.cpp
    GUI/SavePresetDialog.hpp
    GUI/SavePresetDialog.cpp
    GUI/PhysicalPrinterDialog.hpp
    GUI/PhysicalPrinterDialog.cpp
    GUI/GUI_Factories.cpp
    GUI/GUI_Factories.hpp
    GUI/GUI_ObjectList.cpp
    GUI/GUI_ObjectList.hpp
    GUI/GalleryDialog.cpp
    GUI/GalleryDialog.hpp
    GUI/GUI_ObjectManipulation.cpp
    GUI/GUI_ObjectManipulation.hpp
    GUI/GUI_ObjectSettings.cpp
    GUI/GUI_ObjectSettings.hpp
    GUI/GUI_ObjectLayers.cpp
    GUI/GUI_ObjectLayers.hpp
    GUI/MeshUtils.cpp
    GUI/MeshUtils.hpp
    GUI/Tab.cpp
    GUI/Tab.hpp
    GUI/ConfigManipulation.cpp
    GUI/ConfigManipulation.hpp
    GUI/Field.cpp
    GUI/Field.hpp
    GUI/OptionsGroup.cpp
    GUI/OptionsGroup.hpp
    GUI/OG_CustomCtrl.cpp
    GUI/OG_CustomCtrl.hpp
    GUI/BedShapeDialog.cpp
    GUI/BedShapeDialog.hpp
    GUI/2DBed.cpp
    GUI/2DBed.hpp
    GUI/3DBed.cpp
    GUI/3DBed.hpp
    GUI/CoordAxes.cpp
    GUI/CoordAxes.hpp
    GUI/Camera.cpp
    GUI/Camera.hpp
    GUI/CameraUtils.cpp
    GUI/CameraUtils.hpp
    GUI/wxExtensions.cpp
    GUI/wxExtensions.hpp
    GUI/ExtruderSequenceDialog.cpp
    GUI/ExtruderSequenceDialog.hpp
    GUI/WipeTowerDialog.cpp
    GUI/WipeTowerDialog.hpp
    GUI/RammingChart.cpp
    GUI/RammingChart.hpp
    GUI/RemovableDriveManager.cpp
    GUI/RemovableDriveManager.hpp
    GUI/SendSystemInfoDialog.cpp
    GUI/SendSystemInfoDialog.hpp
    GUI/SurfaceDrag.cpp
    GUI/SurfaceDrag.hpp
    GUI/TextLines.cpp
    GUI/TextLines.hpp
    GUI/BonjourDialog.cpp
    GUI/BonjourDialog.hpp
    GUI/ButtonsDescription.cpp
    GUI/ButtonsDescription.hpp
    GUI/ImGuiWrapper.hpp
    GUI/ImGuiWrapper.cpp
    Config/Snapshot.cpp
    Config/Snapshot.hpp
    Config/Version.cpp
    Config/Version.hpp    
    Utils/ASCIIFolding.cpp
    Utils/ASCIIFolding.hpp
    Utils/Serial.cpp
    Utils/Serial.hpp
    GUI/ConfigWizard.cpp
    GUI/ConfigWizard.hpp
    GUI/ConfigWizard_private.hpp
    GUI/MsgDialog.cpp
    GUI/MsgDialog.hpp
    GUI/UpdateDialogs.cpp
    GUI/UpdateDialogs.hpp
    GUI/FirmwareDialog.cpp
    GUI/FirmwareDialog.hpp
    GUI/PrintHostDialogs.cpp
    GUI/PrintHostDialogs.hpp
    GUI/WifiConfigDialog.cpp
    GUI/WifiConfigDialog.hpp
    GUI/Jobs/Job.hpp
    GUI/Jobs/Worker.hpp
    GUI/Jobs/BoostThreadWorker.hpp
    GUI/Jobs/BoostThreadWorker.cpp
    GUI/Jobs/UIThreadWorker.hpp
    GUI/Jobs/BusyCursorJob.hpp
    GUI/Jobs/CancellableJob.hpp
    GUI/Jobs/PlaterWorker.hpp
    GUI/Jobs/ArrangeJob2.hpp
    GUI/Jobs/ArrangeJob2.cpp
    GUI/Jobs/CreateFontNameImageJob.cpp
    GUI/Jobs/CreateFontNameImageJob.hpp
    GUI/Jobs/CreateFontStyleImagesJob.cpp
    GUI/Jobs/CreateFontStyleImagesJob.hpp
    GUI/Jobs/EmbossJob.cpp
    GUI/Jobs/EmbossJob.hpp
    GUI/Jobs/RotoptimizeJob.hpp
    GUI/Jobs/RotoptimizeJob.cpp
    GUI/Jobs/SLAImportJob.hpp
    GUI/Jobs/SLAImportJob.cpp
    GUI/Jobs/ProgressIndicator.hpp
    GUI/Jobs/NotificationProgressIndicator.hpp
    GUI/Jobs/NotificationProgressIndicator.cpp
    GUI/Jobs/ThreadSafeQueue.hpp
    GUI/Jobs/SLAImportDialog.hpp
    GUI/ProgressStatusBar.hpp
    GUI/ProgressStatusBar.cpp
    GUI/Mouse3DController.cpp
    GUI/Mouse3DController.hpp
    GUI/DoubleSlider.cpp
    GUI/DoubleSlider.hpp
    GUI/Notebook.cpp
    GUI/Notebook.hpp
    GUI/ObjectDataViewModel.cpp
    GUI/ObjectDataViewModel.hpp
    GUI/InstanceCheck.cpp
    GUI/InstanceCheck.hpp
    GUI/Search.cpp
    GUI/Search.hpp
    GUI/NotificationManager.cpp
    GUI/NotificationManager.hpp
    GUI/UnsavedChangesDialog.cpp
    GUI/UnsavedChangesDialog.hpp
    GUI/ExtraRenderers.cpp
    GUI/ExtraRenderers.hpp
    GUI/ProjectDirtyStateManager.hpp
    GUI/ProjectDirtyStateManager.cpp
    GUI/DesktopIntegrationDialog.cpp
    GUI/DesktopIntegrationDialog.hpp
    GUI/HintNotification.cpp
    GUI/HintNotification.hpp
    GUI/Widgets/BitmapToggleButton.cpp
    GUI/Widgets/BitmapToggleButton.hpp
    GUI/Widgets/Button.cpp
    GUI/Widgets/Button.hpp
    GUI/Widgets/CheckBox.cpp
    GUI/Widgets/CheckBox.hpp
    GUI/Widgets/ComboBox.cpp
    GUI/Widgets/ComboBox.hpp
    GUI/Widgets/DropDown.cpp
    GUI/Widgets/DropDown.hpp
    GUI/Widgets/StateColor.cpp
    GUI/Widgets/StateColor.hpp
    GUI/Widgets/StateHandler.cpp
    GUI/Widgets/StateHandler.hpp
    GUI/Widgets/StaticBox.cpp
    GUI/Widgets/StaticBox.hpp
    GUI/Widgets/SpinInput.cpp
    GUI/Widgets/SpinInput.hpp
    GUI/Widgets/SwitchButton.cpp
    GUI/Widgets/SwitchButton.hpp
    GUI/Widgets/TextInput.cpp
    GUI/Widgets/TextInput.hpp
    GUI/Widgets/UIColors.hpp
    GUI/FileArchiveDialog.cpp
    GUI/FileArchiveDialog.hpp
    GUI/Downloader.cpp
    GUI/Downloader.hpp
    GUI/DownloaderFileGet.cpp
    GUI/DownloaderFileGet.hpp
    Utils/AppUpdater.cpp
    Utils/AppUpdater.hpp
    Utils/Http.cpp
    Utils/Http.hpp
    Utils/FixModelByWin10.cpp
    Utils/FixModelByWin10.hpp
    Utils/Moonraker.cpp
    Utils/Moonraker.hpp
    Utils/OctoPrint.cpp
    Utils/OctoPrint.hpp
    Utils/Duet.cpp
    Utils/Duet.hpp
    Utils/EmbossStyleManager.cpp
    Utils/EmbossStyleManager.hpp
    Utils/FlashAir.cpp
    Utils/FlashAir.hpp
    Utils/FontConfigHelp.cpp
    Utils/FontConfigHelp.hpp
    Utils/AstroBox.cpp
    Utils/AstroBox.hpp
    Utils/Repetier.cpp
    Utils/Repetier.hpp
    Utils/PrintHost.cpp
    Utils/PrintHost.hpp
    Utils/Bonjour.cpp
    Utils/Bonjour.hpp
    Utils/PresetUpdater.cpp
    Utils/PresetUpdater.hpp
    Utils/Process.cpp
    Utils/Process.hpp
    Utils/RaycastManager.cpp
    Utils/RaycastManager.hpp
    Utils/UndoRedo.cpp
    Utils/UndoRedo.hpp
    Utils/HexFile.cpp
    Utils/HexFile.hpp
    Utils/TCPConsole.cpp
    Utils/TCPConsole.hpp
    Utils/MKS.cpp
    Utils/MKS.hpp
    Utils/WinRegistry.cpp
    Utils/WinRegistry.hpp
    Utils/WxFontUtils.cpp
    Utils/WxFontUtils.hpp
    Utils/WifiScanner.hpp
    Utils/WifiScanner.cpp
)

find_package(NanoSVG REQUIRED)

if (APPLE)
    list(APPEND SLIC3R_GUI_SOURCES
            Utils/RetinaHelperImpl.mm
            Utils/MacDarkMode.mm
            Utils/MacUtils.mm
            Utils/WifiScannerMac.h
            Utils/WifiScannerMac.mm
            GUI/RemovableDriveManagerMM.mm
            GUI/RemovableDriveManagerMM.h
            GUI/Mouse3DHandlerMac.mm
            GUI/InstanceCheckMac.mm
            GUI/InstanceCheckMac.h
        )
    FIND_LIBRARY(DISKARBITRATION_LIBRARY DiskArbitration)
    FIND_LIBRARY(COREWLAN_LIBRARY CoreWLAN)
endif ()

add_library(libslic3r_gui STATIC ${SLIC3R_GUI_SOURCES})

foreach(_source IN ITEMS ${SLIC3R_GUI_SOURCES})
    get_filename_component(_source_path "${_source}" PATH)
    string(REPLACE "/" "\\" _group_path "${_source_path}")
    source_group("${_group_path}" FILES "${_source}")
endforeach()

encoding_check(libslic3r_gui)

target_link_libraries(libslic3r_gui libslic3r avrdude libcereal imgui GLEW::GLEW OpenGL::GL hidapi libcurl ${wxWidgets_LIBRARIES} NanoSVG::nanosvg NanoSVG::nanosvgrast)

if (MSVC)
    target_link_libraries(libslic3r_gui Setupapi.lib)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(libslic3r_gui ${DBUS_LIBRARIES}) 
elseif (APPLE)
    target_link_libraries(libslic3r_gui ${DISKARBITRATION_LIBRARY} ${COREWLAN_LIBRARY})
endif()

#if (SLIC3R_STATIC)
    # FIXME: This was previously exported by wx-config but the wxWidgets
    # cmake build forgets this and the build fails in debug mode (or on raspberry release)
#    target_compile_definitions(libslic3r_gui PUBLIC -DwxDEBUG_LEVEL=0)
#endif()

if (SLIC3R_PCH AND NOT SLIC3R_SYNTAXONLY)
    add_precompiled_header(libslic3r_gui pchheader.hpp FORCEINCLUDE)
endif ()

# We need to implement some hacks for wxWidgets and touch the underlying GTK
# layer and sub-libraries. This forces us to use the include locations and
# link these libraries.
if (UNIX AND NOT APPLE)
    find_package(GTK${SLIC3R_GTK} REQUIRED)
    target_include_directories(libslic3r_gui PRIVATE ${GTK${SLIC3R_GTK}_INCLUDE_DIRS})
    target_link_libraries(libslic3r_gui ${GTK${SLIC3R_GTK}_LIBRARIES} fontconfig)
endif ()

# Add a definition so that we can tell we are compiling slic3r.
target_compile_definitions(libslic3r_gui PRIVATE SLIC3R_CURRENTLY_COMPILING_GUI_MODULE)
