//-----------------------------------------------------------------------------
// COMPILE-TIME OPTIONS FOR DEAR IMGUI
// Runtime options (clipboard callbacks, enabling various features, etc.) can generally be set via the ImGuiIO structure.
// You can use ImGui::SetAllocatorFunctions() before calling ImGui::CreateContext() to rewire memory allocation functions.
//-----------------------------------------------------------------------------
// A) You may edit imconfig.h (and not overwrite it when updating Dear ImGui, or maintain a patch/rebased branch with your modifications to it)
// B) or '#define IMGUI_USER_CONFIG "my_imgui_config.h"' in your project and then add directives in your own file without touching this template.
//-----------------------------------------------------------------------------
// You need to make sure that configuration settings are defined consistently _everywhere_ Dear ImGui is used, which include the imgui*.cpp
// files but also _any_ of your code that uses Dear ImGui. This is because some compile-time options have an affect on data structures.
// Defining those options in imconfig.h will ensure every compilation unit gets to see the same data structure layouts.
// Call IMGUI_CHECKVERSION() from your .cpp files to verify that the data structures your files are using are matching the ones imgui.cpp is using.
//-----------------------------------------------------------------------------

#pragma once

//---- Define assertion handler. Defaults to calling assert().
// If your macro uses multiple statements, make sure is enclosed in a 'do { .. } while (0)' block so it can be used as a single statement.
//#define IM_ASSERT(_EXPR)  MyAssert(_EXPR)
//#define IM_ASSERT(_EXPR)  ((void)(_EXPR))     // Disable asserts

//---- Define attributes of all API symbols declarations, e.g. for DLL under Windows
// Using Dear ImGui via a shared library is not recommended, because of function call overhead and because we don't guarantee backward nor forward ABI compatibility.
// DLL users: heaps and globals are not shared across DLL boundaries! You will need to call SetCurrentContext() + SetAllocatorFunctions()
// for each static/DLL boundary you are calling from. Read "Context and Memory Allocators" section of imgui.cpp for more details.
//#define IMGUI_API __declspec( dllexport )
//#define IMGUI_API __declspec( dllimport )

//---- Don't define obsolete functions/enums/behaviors. Consider enabling from time to time after updating to avoid using soon-to-be obsolete function/names.
//#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS

//---- Disable all of Dear ImGui or don't implement standard windows.
// It is very strongly recommended to NOT disable the demo windows during development. Please read comments in imgui_demo.cpp.
//#define IMGUI_DISABLE                                     // Disable everything: all headers and source files will be empty.
//#define IMGUI_DISABLE_DEMO_WINDOWS                        // Disable demo windows: ShowDemoWindow()/ShowStyleEditor() will be empty. Not recommended.
//#define IMGUI_DISABLE_METRICS_WINDOW                      // Disable metrics/debugger window: ShowMetricsWindow() will be empty.

//---- Don't implement some functions to reduce linkage requirements.
//#define IMGUI_DISABLE_WIN32_DEFAULT_CLIPBOARD_FUNCTIONS   // [Win32] Don't implement default clipboard handler. Won't use and link with OpenClipboard/GetClipboardData/CloseClipboard etc. (user32.lib/.a, kernel32.lib/.a)
//#define IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS         // [Win32] Don't implement default IME handler. Won't use and link with ImmGetContext/ImmSetCompositionWindow. (imm32.lib/.a)
//#define IMGUI_DISABLE_WIN32_FUNCTIONS                     // [Win32] Won't use and link with any Win32 function (clipboard, ime).
//#define IMGUI_ENABLE_OSX_DEFAULT_CLIPBOARD_FUNCTIONS      // [OSX] Implement default OSX clipboard handler (need to link with '-framework ApplicationServices', this is why this is not the default).
//#define IMGUI_DISABLE_DEFAULT_FORMAT_FUNCTIONS            // Don't implement ImFormatString/ImFormatStringV so you can implement them yourself (e.g. if you don't want to link with vsnprintf)
//#define IMGUI_DISABLE_DEFAULT_MATH_FUNCTIONS              // Don't implement ImFabs/ImSqrt/ImPow/ImFmod/ImCos/ImSin/ImAcos/ImAtan2 so you can implement them yourself.
//#define IMGUI_DISABLE_FILE_FUNCTIONS                      // Don't implement ImFileOpen/ImFileClose/ImFileRead/ImFileWrite and ImFileHandle at all (replace them with dummies)
//#define IMGUI_DISABLE_DEFAULT_FILE_FUNCTIONS              // Don't implement ImFileOpen/ImFileClose/ImFileRead/ImFileWrite and ImFileHandle so you can implement them yourself if you don't want to link with fopen/fclose/fread/fwrite. This will also disable the LogToTTY() function.
//#define IMGUI_DISABLE_DEFAULT_ALLOCATORS                  // Don't implement default allocators calling malloc()/free() to avoid linking with them. You will need to call ImGui::SetAllocatorFunctions().

//---- Include imgui_user.h at the end of imgui.h as a convenience
//#define IMGUI_INCLUDE_IMGUI_USER_H

//---- Pack colors to BGRA8 instead of RGBA8 (to avoid converting from one to another)
//#define IMGUI_USE_BGRA_PACKED_COLOR

//---- Use 32-bit for ImWchar (default is 16-bit) to support unicode planes 1-16. (e.g. point beyond 0xFFFF like emoticons, dingbats, symbols, shapes, ancient languages, etc...)
//#define IMGUI_USE_WCHAR32

//---- Avoid multiple STB libraries implementations, or redefine path/filenames to prioritize another version
// By default the embedded implementations are declared static and not available outside of Dear ImGui sources files.
//#define IMGUI_STB_TRUETYPE_FILENAME   "my_folder/stb_truetype.h"
//#define IMGUI_STB_RECT_PACK_FILENAME  "my_folder/stb_rect_pack.h"
//#define IMGUI_DISABLE_STB_TRUETYPE_IMPLEMENTATION
//#define IMGUI_DISABLE_STB_RECT_PACK_IMPLEMENTATION

//---- Use stb_printf's faster implementation of vsnprintf instead of the one from libc (unless IMGUI_DISABLE_DEFAULT_FORMAT_FUNCTIONS is defined)
// Requires 'stb_sprintf.h' to be available in the include path. Compatibility checks of arguments and formats done by clang and GCC will be disabled in order to support the extra formats provided by STB sprintf.
// #define IMGUI_USE_STB_SPRINTF

//---- Use FreeType to build and rasterize the font atlas (instead of stb_truetype which is embedded by default in Dear ImGui)
// Requires FreeType headers to be available in the include path. Requires program to be compiled with 'misc/freetype/imgui_freetype.cpp' (in this repository) + the FreeType library (not provided).
// On Windows you may use vcpkg with 'vcpkg install freetype' + 'vcpkg integrate install'.
//#define IMGUI_ENABLE_FREETYPE

//---- Use stb_truetype to build and rasterize the font atlas (default)
// The only purpose of this define is if you want force compilation of the stb_truetype backend ALONG with the FreeType backend.
//#define IMGUI_ENABLE_STB_TRUETYPE

//---- Define constructor and implicit cast operators to convert back<>forth between your math types and ImVec2/ImVec4.
// This will be inlined as part of ImVec2 and ImVec4 class declarations.
/*
#define IM_VEC2_CLASS_EXTRA                                                 \
        ImVec2(const MyVec2& f) { x = f.x; y = f.y; }                       \
        operator MyVec2() const { return MyVec2(x,y); }

#define IM_VEC4_CLASS_EXTRA                                                 \
        ImVec4(const MyVec4& f) { x = f.x; y = f.y; z = f.z; w = f.w; }     \
        operator MyVec4() const { return MyVec4(x,y,z,w); }
*/

//---- Use 32-bit vertex indices (default is 16-bit) is one way to allow large meshes with more than 64K vertices.
// Your renderer backend will need to support it (most example renderer backends support both 16/32-bit indices).
// Another way to allow large meshes while keeping 16-bit indices is to handle ImDrawCmd::VtxOffset in your renderer.
// Read about ImGuiBackendFlags_RendererHasVtxOffset for details.
//#define ImDrawIdx unsigned int

//---- Override ImDrawCallback signature (will need to modify renderer backends accordingly)
//struct ImDrawList;
//struct ImDrawCmd;
//typedef void (*MyImDrawCallback)(const ImDrawList* draw_list, const ImDrawCmd* cmd, void* my_renderer_user_data);
//#define ImDrawCallback MyImDrawCallback

//---- Debug Tools: Macro to break in Debugger
// (use 'Metrics->Tools->Item Picker' to pick widgets with the mouse and break into them for easy debugging.)
//#define IM_DEBUG_BREAK  IM_ASSERT(0)
//#define IM_DEBUG_BREAK  __debugbreak()

//---- Debug Tools: Have the Item Picker break in the ItemAdd() function instead of ItemHoverable(),
// (which comes earlier in the code, will catch a few extra items, allow picking items other than Hovered one.)
// This adds a small runtime cost which is why it is not enabled by default.
//#define IMGUI_DEBUG_TOOL_ITEM_PICKER_EX

//---- Debug Tools: Enable slower asserts
//#define IMGUI_DEBUG_PARANOID

//---- Tip: You can add extra functions within the ImGui:: namespace, here or in your own headers files.

namespace ImGui
{
    // Special ASCII character is used here as markup symbols for tokens to be highlighted as a for hovered item
    const char ColorMarkerHovered   = 0x1; // STX

    // Special ASCII characters STX and ETX are used here as markup symbols for tokens to be highlighted.
    const char ColorMarkerStart = 0x2; // STX
    const char ColorMarkerEnd   = 0x3; // ETX

    // Special ASCII characters are used here as an ikons markers
    const wchar_t PrintIconMarker          = 0x4;
    const wchar_t PrinterIconMarker        = 0x5;
    const wchar_t PrinterSlaIconMarker     = 0x6;
    const wchar_t FilamentIconMarker       = 0x7;
    const wchar_t MaterialIconMarker       = 0x8;
    const wchar_t CloseNotifButton         = 0xB;
    const wchar_t CloseNotifHoverButton    = 0xC;
    const wchar_t MinimalizeButton         = 0xE;
    const wchar_t MinimalizeHoverButton    = 0xF;
    const wchar_t WarningMarker            = 0x10;
    const wchar_t ErrorMarker              = 0x11;
    const wchar_t EjectButton              = 0x12;
    const wchar_t EjectHoverButton         = 0x13;
    const wchar_t CancelButton             = 0x14;
    const wchar_t CancelHoverButton        = 0x15;
//    const wchar_t VarLayerHeightMarker     = 0x16;
    const wchar_t RevertButton             = 0x16;

    const wchar_t RightArrowButton         = 0x18;
    const wchar_t RightArrowHoverButton    = 0x19;
    const wchar_t PreferencesButton        = 0x1A;
    const wchar_t PreferencesHoverButton   = 0x1B;
//    const wchar_t SinkingObjectMarker      = 0x1C;
//    const wchar_t CustomSupportsMarker     = 0x1D;
//    const wchar_t CustomSeamMarker         = 0x1E;
//    const wchar_t MmuSegmentationMarker    = 0x1F;
    const wchar_t PlugMarker               = 0x1C;
    const wchar_t DowelMarker              = 0x1D;
    const wchar_t SnapMarker               = 0x1E;
    const wchar_t HorizontalHide           = 0xB4;
    const wchar_t HorizontalShow           = 0xB6;
    // Do not forget use following letters only in wstring
    const wchar_t DocumentationButton      = 0x2600;
    const wchar_t DocumentationHoverButton = 0x2601;
    const wchar_t ClippyMarker             = 0x2602;
    const wchar_t InfoMarker               = 0x2603;
    const wchar_t SliderFloatEditBtnIcon   = 0x2604;
    const wchar_t SliderFloatEditBtnPressedIcon = 0x2605;
    const wchar_t ClipboardBtnIcon         = 0x2606;
    const wchar_t PlayButton               = 0x2618;
    const wchar_t PlayHoverButton          = 0x2619;
    const wchar_t PauseButton              = 0x261A;
    const wchar_t PauseHoverButton         = 0x261B;
    const wchar_t OpenButton               = 0x261C;
    const wchar_t OpenHoverButton          = 0x261D;
    const wchar_t SlaViewOriginal          = 0x261E;
    const wchar_t SlaViewProcessed         = 0x261F;

    const wchar_t LegendTravel             = 0x2701;
    const wchar_t LegendWipe               = 0x2702;
    const wchar_t LegendRetract            = 0x2703;
    const wchar_t LegendDeretract          = 0x2704;
    const wchar_t LegendSeams              = 0x2705;
    const wchar_t LegendToolChanges        = 0x2706;
    const wchar_t LegendColorChanges       = 0x2707;
    const wchar_t LegendPausePrints        = 0x2708;
    const wchar_t LegendCustomGCodes       = 0x2709;
    const wchar_t LegendCOG                = 0x2710;
    const wchar_t LegendShells             = 0x2711;
    const wchar_t LegendToolMarker         = 0x2712;
    const wchar_t WarningMarkerSmall       = 0x2713;
    const wchar_t ExpandBtn                = 0x2714;
    const wchar_t InfoMarkerSmall          = 0x2716;
    const wchar_t CollapseBtn              = 0x2715;

    // icons for double slider (middle size icons)
    const wchar_t Lock                     = 0x2801;
    const wchar_t LockHovered              = 0x2802;
    const wchar_t Unlock                   = 0x2803;
    const wchar_t UnlockHovered            = 0x2804;
    const wchar_t DSRevert                 = 0x2805;
    const wchar_t DSRevertHovered          = 0x2806;
    const wchar_t DSSettings               = 0x2807;
    const wchar_t DSSettingsHovered        = 0x2808;
    // icons for double slider (small size icons)
    const wchar_t ErrorTick                = 0x2809;
    const wchar_t ErrorTickHovered         = 0x280A;
    const wchar_t PausePrint               = 0x280B;
    const wchar_t PausePrintHovered        = 0x280C;
    const wchar_t EditGCode                = 0x280D;
    const wchar_t EditGCodeHovered         = 0x280E;
    const wchar_t RemoveTick               = 0x280F;
    const wchar_t RemoveTickHovered        = 0x2810;

    // icon for multiple beds
    const wchar_t SliceAllBtnIcon          = 0x2811;
    const wchar_t PrintIdle                = 0x2812;
    const wchar_t PrintRunning             = 0x2813;
    const wchar_t PrintFinished            = 0x2814;
    const wchar_t WarningMarkerDisabled    = 0x2815;
    //    void MyFunction(const char* name, const MyMatrix44& v);
}

