///|/ Copyright (c) Prusa Research 2018 - 2023 Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, David Kocík @kocikdav, Vojtěch Bubník @bubnikv, Tomáš Mészáros @tamasmeszaros, Filip Sykala @Jony01, Lukáš Hejl @hejllukas, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2019 Jason Tibbitts @jasontibbitts
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "ImGuiWrapper.hpp"

#include <cstdio>
#include <vector>
#include <cmath>
#include <stdexcept>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/nowide/convert.hpp>

#include <wx/string.h>
#include <wx/event.h>
#include <wx/clipbrd.h>
#include <wx/debug.h>

#include <GL/glew.h>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Color.hpp"

#include "3DScene.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
#include "Search.hpp"
#include "BitmapCache.hpp"
#include "GUI_App.hpp"

#include "../Utils/MacDarkMode.hpp"
#include <nanosvg/nanosvg.h>
#include <nanosvg/nanosvgrast.h>

// suggest location
#include "libslic3r/ClipperUtils.hpp" // Slic3r::intersection


// Following two sets keeps characters that ImGui tried to render, but they were not in the atlas,
// and ones that we already tried to add into the atlas.
std::set<ImWchar> s_missing_chars;
std::set<ImWchar> s_fixed_chars;
bool              s_font_cjk;

// This is a free function that ImGui calls when it renders
// a fallback glyph for c.
void imgui_rendered_fallback_glyph(ImWchar c)
{
    if (c == 0)
        return;

    if (ImGui::GetIO().Fonts->Fonts[0] == ImGui::GetFont()) {
        // Only do this when we are using the default ImGui font. Otherwise this would conflict with
        // EmbossStyleManager's font handling and we would load glyphs needlessly.
        auto it = s_fixed_chars.find(c);
        if (it == s_fixed_chars.end()) {
            // This is the first time we are trying to fix this character.
            s_missing_chars.emplace(c);
        } else {
            // We already tried to add this, but it is still not there. There is a chance
            // that loading the CJK font would make this available.
            if (! s_font_cjk) {
                s_font_cjk = true;
                s_missing_chars.emplace(c);
                s_fixed_chars.erase(it);
            } else {
                // We did everything we could. The glyph was not available.
                // Do not try to add it anymore.
            }
        }
    }
}


namespace Slic3r {
namespace GUI {


static const std::map<const wchar_t, std::string> font_icons = {
    {ImGui::PrintIconMarker       , "cog"                           },
    {ImGui::PrinterIconMarker     , "printer"                       },
    {ImGui::PrinterSlaIconMarker  , "sla_printer"                   },
    {ImGui::FilamentIconMarker    , "spool"                         },
    {ImGui::MaterialIconMarker    , "resin"                         },
    {ImGui::MinimalizeButton      , "notification_minimalize"       },
    {ImGui::MinimalizeHoverButton , "notification_minimalize_hover" },
    {ImGui::RightArrowButton      , "notification_right"            },
    {ImGui::RightArrowHoverButton , "notification_right_hover"      },
    {ImGui::PreferencesButton     , "notification_preferences"      },
    {ImGui::PreferencesHoverButton, "notification_preferences_hover"},
    {ImGui::SliderFloatEditBtnIcon, "edit_button"                   },
    {ImGui::SliderFloatEditBtnPressedIcon, "edit_button_pressed"    },
    {ImGui::ClipboardBtnIcon      , "copy_menu"                     },
    {ImGui::ExpandBtn             , "expand_btn"                    },
    {ImGui::CollapseBtn           , "collapse_btn"                  },
    {ImGui::RevertButton          , "undo"                          },
    {ImGui::WarningMarkerSmall    , "notification_warning"          },
    {ImGui::InfoMarkerSmall       , "notification_info"             },
    {ImGui::PlugMarker            , "plug"                          },
    {ImGui::DowelMarker           , "dowel"                         },
    {ImGui::SnapMarker            , "snap"                          },
    {ImGui::HorizontalHide        , "horizontal_hide"               },
    {ImGui::HorizontalShow        , "horizontal_show"               },
    {ImGui::PrintIdle             , "print_idle"                    },
    {ImGui::PrintRunning          , "print_running"                 },
    {ImGui::PrintFinished         , "print_finished"                },
};

static const std::map<const wchar_t, std::string> font_icons_large = {
    {ImGui::LegendTravel            , "legend_travel"                   },
    {ImGui::LegendWipe              , "legend_wipe"                     },
    {ImGui::LegendRetract           , "legend_retract"                  },
    {ImGui::LegendDeretract         , "legend_deretract"                },
    {ImGui::LegendSeams             , "legend_seams"                    },
    {ImGui::LegendToolChanges       , "legend_toolchanges"              },
    {ImGui::LegendColorChanges      , "legend_colorchanges"             },
    {ImGui::LegendPausePrints       , "legend_pauseprints"              },
    {ImGui::LegendCustomGCodes      , "legend_customgcodes"             },
    {ImGui::LegendCOG               , "legend_cog"                      },
    {ImGui::LegendShells            , "legend_shells"                   },
    {ImGui::LegendToolMarker        , "legend_toolmarker"               },
    {ImGui::CloseNotifButton        , "notification_close"              },
    {ImGui::CloseNotifHoverButton   , "notification_close_hover"        },
    {ImGui::EjectButton             , "notification_eject_sd"           },
    {ImGui::EjectHoverButton        , "notification_eject_sd_hover"     },
    {ImGui::WarningMarker           , "notification_warning"            },
    {ImGui::ErrorMarker             , "notification_error"              },
    {ImGui::CancelButton            , "notification_cancel"             },
    {ImGui::CancelHoverButton       , "notification_cancel_hover"       },
//    {ImGui::SinkingObjectMarker     , "move"                            },
//    {ImGui::CustomSupportsMarker    , "fdm_supports"                    },
//    {ImGui::CustomSeamMarker        , "seam"                            },
//    {ImGui::MmuSegmentationMarker   , "mmu_segmentation"                },
//    {ImGui::VarLayerHeightMarker    , "layers"                          },
    {ImGui::DocumentationButton     , "notification_documentation"      },
    {ImGui::DocumentationHoverButton, "notification_documentation_hover"},
    {ImGui::InfoMarker              , "notification_info"               },
    {ImGui::PlayButton              , "notification_play"               },
    {ImGui::PlayHoverButton         , "notification_play_hover"         },
    {ImGui::PauseButton             , "notification_pause"              },
    {ImGui::PauseHoverButton        , "notification_pause_hover"        },
    {ImGui::OpenButton              , "notification_open"               },
    {ImGui::OpenHoverButton         , "notification_open_hover"         },
    {ImGui::SlaViewOriginal         , "sla_view_original"               },
    {ImGui::SlaViewProcessed        , "sla_view_processed"              },
};

static const std::map<const wchar_t, std::string> font_icons_medium = {
    {ImGui::Lock                    , "lock_closed"                     },
    {ImGui::LockHovered             , "lock_closed_f"                   },
    {ImGui::Unlock                  , "lock_open"                       },
    {ImGui::UnlockHovered           , "lock_open_f"                     },
    {ImGui::DSRevert                , "undo"                            },
    {ImGui::DSRevertHovered         , "undo_f"                          },
    {ImGui::DSSettings              , "cog"                             },
    {ImGui::DSSettingsHovered       , "cog_f"                           },

    {ImGui::ErrorTick               , "error_tick"                      },
    {ImGui::ErrorTickHovered        , "error_tick_f"                    },
    {ImGui::PausePrint              , "pause_print"                     },
    {ImGui::PausePrintHovered       , "pause_print_f"                   },
    {ImGui::EditGCode               , "edit_gcode"                      },
    {ImGui::EditGCodeHovered        , "edit_gcode_f"                    },
    {ImGui::RemoveTick              , "colorchange_del"                 },
    {ImGui::RemoveTickHovered       , "colorchange_del_f"               },
};

static const std::map<const wchar_t, std::string> font_icons_extra_large = {
    {ImGui::ClippyMarker            , "notification_clippy"             },
    {ImGui::SliceAllBtnIcon         , "slice_all"                       },
    {ImGui::WarningMarkerDisabled   , "notification_warning_grey"       },
};

ImGuiWrapper::ImGuiWrapper()
{
    ImGui::CreateContext();

    init_input();
    init_style();

    ImGui::GetIO().IniFilename = nullptr;

    static const ImWchar ranges_latin2[] =
    {
        0x0020, 0x00FF, // Basic Latin + Latin Supplement
        0x0100, 0x017F, // Latin Extended-A
        0,
    };
    static const ImWchar ranges_turkish[] = {
	    0x0020, 0x01FF, // Basic Latin + Latin Supplement
	    0x0100, 0x017F, // Latin Extended-A
	    0x0180, 0x01FF, // Turkish
	    0,
    };
    static const ImWchar ranges_vietnamese[] =
    {
        0x0020, 0x00FF, // Basic Latin
        0x0102, 0x0103,
        0x0110, 0x0111,
        0x0128, 0x0129,
        0x0168, 0x0169,
        0x01A0, 0x01A1,
        0x01AF, 0x01B0,
        0x1EA0, 0x1EF9,
        0,
    };

    m_lang_glyphs_info.emplace_back("cs",   ranges_latin2, false);
    m_lang_glyphs_info.emplace_back("pl",   ranges_latin2, false);
    m_lang_glyphs_info.emplace_back("hu",   ranges_latin2, false);
    m_lang_glyphs_info.emplace_back("sl",   ranges_latin2, false);
    m_lang_glyphs_info.emplace_back("ru",   ImGui::GetIO().Fonts->GetGlyphRangesCyrillic(), false); // Default + about 400 Cyrillic characters
    m_lang_glyphs_info.emplace_back("uk",   ImGui::GetIO().Fonts->GetGlyphRangesCyrillic(), false);
    m_lang_glyphs_info.emplace_back("be",   ImGui::GetIO().Fonts->GetGlyphRangesCyrillic(), false);
    m_lang_glyphs_info.emplace_back("tr",   ranges_turkish,    false);
    m_lang_glyphs_info.emplace_back("vi",   ranges_vietnamese, false);
    m_lang_glyphs_info.emplace_back("ja",   ImGui::GetIO().Fonts->GetGlyphRangesJapanese(), true);         // Default + Hiragana, Katakana, Half-Width, Selection of 1946 Ideographs
    m_lang_glyphs_info.emplace_back("ko",   ImGui::GetIO().Fonts->GetGlyphRangesKorean(),   true);         // Default + Korean characters
    m_lang_glyphs_info.emplace_back("zh_TW",ImGui::GetIO().Fonts->GetGlyphRangesChineseFull(), true);      // Traditional Chinese: Default + Half-Width + Japanese Hiragana/Katakana + full set of about 21000 CJK Unified Ideographs
    m_lang_glyphs_info.emplace_back("zh",   ImGui::GetIO().Fonts->GetGlyphRangesChineseSimplifiedCommon(), true); // Simplified Chinese: Default + Half-Width + Japanese Hiragana/Katakana + set of 2500 CJK Unified Ideographs for common simplified Chinese
    m_lang_glyphs_info.emplace_back("th",   ImGui::GetIO().Fonts->GetGlyphRangesThai(),     false);
    m_lang_glyphs_info.emplace_back("else", ImGui::GetIO().Fonts->GetGlyphRangesDefault(),  false);
}

ImGuiWrapper::~ImGuiWrapper()
{
    destroy_font();
    ImGui::DestroyContext();
}

void ImGuiWrapper::set_language(const std::string &language)
{
    if (m_new_frame_open) {
        // ImGUI internally locks the font between NewFrame() and EndFrame()
        // NewFrame() might've been called here because of input from the 3D scene;
        // call EndFrame()
        ImGui::EndFrame();
        m_new_frame_open = false;
    }

    const ImWchar *ranges = nullptr;

    // Get glyph ranges for current language, std CLK flag to inform which font files need to be loaded.
    for (const auto& [lang_str, lang_ranges, lang_cjk] : m_lang_glyphs_info) {
        if (boost::istarts_with(language, lang_str) || lang_str == "else") {
            ranges = lang_ranges;
            s_font_cjk = lang_cjk;
        }
    }

    s_missing_chars.clear();
    s_fixed_chars.clear();

    if (ranges != m_glyph_ranges) {
        m_glyph_ranges = ranges;
        destroy_font();
    }
}

void ImGuiWrapper::set_scaling(float font_size, float scale_style, float scale_both)
{
    font_size *= scale_both;
    scale_style *= scale_both;

    if (m_font_size == font_size && m_style_scaling == scale_style) {
        return;
    }

    m_font_size = font_size;

    ImGui::GetStyle().ScaleAllSizes(scale_style / m_style_scaling);
    m_style_scaling = scale_style;

    destroy_font();
}

bool ImGuiWrapper::update_mouse_data(wxMouseEvent& evt)
{
    if (! display_initialized()) {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.MousePos = ImVec2((float)evt.GetX(), (float)evt.GetY());
    io.MouseDown[0] = evt.LeftIsDown();
    io.MouseDown[1] = evt.RightIsDown();
    io.MouseDown[2] = evt.MiddleIsDown();
    io.MouseDoubleClicked[0] = evt.LeftDClick();
    io.MouseDoubleClicked[1] = evt.RightDClick();
    io.MouseDoubleClicked[2] = evt.MiddleDClick();
    float wheel_delta = static_cast<float>(evt.GetWheelDelta());
    if (wheel_delta != 0.0f)
        io.MouseWheel = static_cast<float>(evt.GetWheelRotation()) / wheel_delta;

    unsigned buttons = (evt.LeftIsDown() ? 1 : 0) | (evt.RightIsDown() ? 2 : 0) | (evt.MiddleIsDown() ? 4 : 0);
    m_mouse_buttons = buttons;

    if (ImGuiPureWrap::want_mouse())
        new_frame();
    return ImGuiPureWrap::want_mouse();
}

bool ImGuiWrapper::update_key_data(wxKeyEvent &evt)
{
    if (! display_initialized()) {
        return false;
    }

    auto to_string = [](wxEventType type) -> std::string {
        if (type == wxEVT_CHAR) return "Char";
        if (type == wxEVT_KEY_DOWN) return "KeyDown";
        if (type == wxEVT_KEY_UP) return "KeyUp";
        return "Other";
    };

    wxEventType type = evt.GetEventType();
    ImGuiIO& io = ImGui::GetIO();
    BOOST_LOG_TRIVIAL(debug) << "ImGui - key event(" << to_string(type) << "):"
                             //<< " Unicode(" << evt.GetUnicodeKey() << ")"
                             << " KeyCode(" << evt.GetKeyCode() << ")";

    if (type == wxEVT_CHAR) {
        // Char event
        const auto   key   = evt.GetUnicodeKey();

        // Release BackSpace, Delete, ... when miss wxEVT_KEY_UP event
        // Already Fixed at begining of new frame
        // unsigned int key_u = static_cast<unsigned int>(key);
        //if (key_u >= 0 && key_u < IM_ARRAYSIZE(io.KeysDown) && io.KeysDown[key_u]) { 
        //    io.KeysDown[key_u] = false;
        //}

        if (key != 0) {
            io.AddInputCharacter(key);
        }
    } else if (type == wxEVT_KEY_DOWN || type == wxEVT_KEY_UP) {
        // Key up/down event
        int key = evt.GetKeyCode();
        wxCHECK_MSG(key >= 0 && key < IM_ARRAYSIZE(io.KeysDown), false, "Received invalid key code");

        io.KeysDown[key] = (type == wxEVT_KEY_DOWN);
        io.KeyShift = evt.ShiftDown();
        io.KeyCtrl = evt.ControlDown();
        io.KeyAlt = evt.AltDown();
        io.KeySuper = evt.MetaDown();
    }
    bool ret = ImGuiPureWrap::want_keyboard() || ImGuiPureWrap::want_text_input();
    if (ret)
        new_frame();
    return ret;
}

#include <array>
void ImGuiWrapper::new_frame()
{
    if (m_new_frame_open) {
        return;
    }

    if (m_font_texture == 0) {
        init_font(true);
    }

    ImGuiIO& io = ImGui::GetIO();
    // synchronize key states
    // when the application loses the focus it may happen that the key up event is not processed

    // synchronize modifier keys
    constexpr std::array<std::pair<ImGuiKeyModFlags_, wxKeyCode>, 3> imgui_mod_keys{
        std::make_pair(ImGuiKeyModFlags_Ctrl, WXK_CONTROL),
        std::make_pair(ImGuiKeyModFlags_Shift, WXK_SHIFT),
        std::make_pair(ImGuiKeyModFlags_Alt, WXK_ALT)};
    for (const std::pair<ImGuiKeyModFlags_, wxKeyCode>& key : imgui_mod_keys) {
        if ((io.KeyMods & key.first) != 0 && !wxGetKeyState(key.second))
            io.KeyMods &= ~key.first;
    }

    // Not sure if it is neccessary
    // values from 33 to 126 are reserved for the standard ASCII characters
    for (size_t i = 33; i <= 126; ++i) { 
        wxKeyCode keycode = static_cast<wxKeyCode>(i);
        if (io.KeysDown[i] && keycode != WXK_NONE && !wxGetKeyState(keycode))
            io.KeysDown[i] = false;
    }

    // special keys: delete, backspace, ...
    for (int key: io.KeyMap) {
        wxKeyCode keycode = static_cast<wxKeyCode>(key);
        if (io.KeysDown[key] && keycode != WXK_NONE && !wxGetKeyState(keycode)) 
            io.KeysDown[key] = false;
    }

    ImGui::NewFrame();
    m_new_frame_open = true;
}

void ImGuiWrapper::render()
{
    ImGui::Render();
    render_draw_data(ImGui::GetDrawData());
    m_new_frame_open = false;

    if (! s_missing_chars.empty()) {
        // If there were some characters that ImGui was unable to render, we will destroy current font.
        // It will be rebuilt in the next call of new_frame including these.
        destroy_font();
        this->set_requires_extra_frame();
    }
}

bool ImGuiWrapper::button(const std::string& label, const ImVec2 &size, bool enable)
{
    disabled_begin(!enable);

    bool res = ImGui::Button(label.c_str(), size);

    disabled_end();
    return (enable) ? res : false;
}

void ImGuiWrapper::draw_icon(ImGuiWindow& window, const ImVec2& pos, float size, wchar_t icon_id)
{
    ImGuiIO& io = ImGui::GetIO();
    const ImTextureID tex_id = io.Fonts->TexID;
    const float tex_w = static_cast<float>(io.Fonts->TexWidth);
    const float tex_h = static_cast<float>(io.Fonts->TexHeight);
    const ImFontAtlas::CustomRect* const rect = GetTextureCustomRect(icon_id);
    const ImVec2 uv0 = { static_cast<float>(rect->X) / tex_w, static_cast<float>(rect->Y) / tex_h };
    const ImVec2 uv1 = { static_cast<float>(rect->X + rect->Width) / tex_w, static_cast<float>(rect->Y + rect->Height) / tex_h };
    window.DrawList->AddImage(tex_id, pos, { pos.x + size, pos.y + size }, uv0, uv1, ImGuiPSWrap::to_ImU32({ 1.0f, 1.0f, 1.0f, 1.0f }));
}

bool ImGuiWrapper::slider_float(const char* label, float* v, float v_min, float v_max, const char* format/* = "%.3f"*/, float power/* = 1.0f*/, bool clamp /*= true*/, const wxString& tooltip /*= ""*/, bool show_edit_btn /*= true*/)
{
    const float max_tooltip_width = ImGui::GetFontSize() * 20.0f;

    // let the label string start with "##" to hide the automatic label from ImGui::SliderFloat()
    bool label_visible = !boost::algorithm::starts_with(label, "##");
    std::string str_label = label_visible ? std::string("##") + std::string(label) : std::string(label);

    // removes 2nd evenience of "##", if present
    std::string::size_type pos = str_label.find("##", 2);
    if (pos != std::string::npos)
        str_label = str_label.substr(0, pos) + str_label.substr(pos + 2);

    // the current slider edit state needs to be detected here before calling SliderFloat()
    bool slider_editing = ImGui::GetCurrentWindow()->GetID(str_label.c_str()) == ImGui::GetActiveID();

    bool ret = ImGui::SliderFloat(str_label.c_str(), v, v_min, v_max, format, power);

    m_last_slider_status.hovered = ImGui::IsItemHovered();
    m_last_slider_status.edited = ImGui::IsItemEdited();
    m_last_slider_status.clicked = ImGui::IsItemClicked();
    m_last_slider_status.deactivated_after_edit = ImGui::IsItemDeactivatedAfterEdit();
    if (!m_last_slider_status.can_take_snapshot)
        m_last_slider_status.can_take_snapshot = ImGui::IsItemClicked();

    if (!tooltip.empty() && ImGui::IsItemHovered())
        ImGuiPureWrap::tooltip(into_u8(tooltip).c_str(), max_tooltip_width);

    if (clamp)
        *v = std::clamp(*v, v_min, v_max);

    const ImGuiStyle& style = ImGui::GetStyle();
    if (show_edit_btn) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 1, style.ItemSpacing.y });
        ImGui::SameLine();

        ImGuiIO& io = ImGui::GetIO();
        assert(io.Fonts->TexWidth > 0 && io.Fonts->TexHeight > 0);
        float inv_tex_w = 1.0f / float(io.Fonts->TexWidth);
        float inv_tex_h = 1.0f / float(io.Fonts->TexHeight);

        const ImFontAtlasCustomRect* const rect = GetTextureCustomRect(slider_editing ? ImGui::SliderFloatEditBtnPressedIcon : ImGui::SliderFloatEditBtnIcon);
        const ImVec2 size = { float(rect->Width), float(rect->Height) };
        const ImVec2 uv0 = ImVec2(float(rect->X) * inv_tex_w, float(rect->Y) * inv_tex_h);
        const ImVec2 uv1 = ImVec2(float(rect->X + rect->Width) * inv_tex_w, float(rect->Y + rect->Height) * inv_tex_h);

        ImGui::PushStyleColor(ImGuiCol_Button, { 0.25f, 0.25f, 0.25f, 0.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.4f, 0.4f, 0.4f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, { 0.4f, 0.4f, 0.4f, 1.0f });

        int frame_padding = style.ItemSpacing.y / 2; // keep same line height for input and slider
        const ImTextureID tex_id = io.Fonts->TexID;
        if (ImGuiPureWrap::image_button(tex_id, size, uv0, uv1, frame_padding, ImVec4(0.0, 0.0, 0.0, 0.0), ImVec4(1.0, 1.0, 1.0, 1.0), ImGuiButtonFlags_PressedOnClick)) {
            if (!slider_editing)
                ImGui::SetKeyboardFocusHere(-1);
            else
                ImGui::ClearActiveID();
            this->set_requires_extra_frame();
        }

        ImGui::PopStyleColor(3);

        if (ImGui::IsItemHovered())
            ImGuiPureWrap::tooltip(into_u8(_L("Edit")).c_str(), max_tooltip_width);

        ImGui::PopStyleVar();
    }

    if (label_visible) {
        // if the label is visible, hide the part of it that should be hidden
        std::string out_label = std::string(label);
        std::string::size_type pos = out_label.find("##");
        if (pos != std::string::npos)
            out_label = out_label.substr(0, pos);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 1, style.ItemSpacing.y });
        ImGui::SameLine();
        ImGuiPureWrap::text(out_label.c_str());
        ImGui::PopStyleVar();
    }

    return ret;
}

bool ImGuiWrapper::slider_float(const std::string& label, float* v, float v_min, float v_max, const char* format/* = "%.3f"*/, float power/* = 1.0f*/, bool clamp /*= true*/, const wxString& tooltip /*= ""*/, bool show_edit_btn /*= true*/)
{
    return this->slider_float(label.c_str(), v, v_min, v_max, format, power, clamp, tooltip, show_edit_btn);
}

bool ImGuiWrapper::slider_float(const wxString& label, float* v, float v_min, float v_max, const char* format/* = "%.3f"*/, float power/* = 1.0f*/, bool clamp /*= true*/, const wxString& tooltip /*= ""*/, bool show_edit_btn /*= true*/)
{
    auto label_utf8 = into_u8(label);
    return this->slider_float(label_utf8.c_str(), v, v_min, v_max, format, power, clamp, tooltip, show_edit_btn);
}

bool ImGuiWrapper::image_button(const wchar_t icon, const std::string& tooltip, bool highlight_on_hover/* = true*/)
{
    const ImGuiIO& io = ImGui::GetIO();
    const ImTextureID tex_id = io.Fonts->TexID;
    assert(io.Fonts->TexWidth > 0 && io.Fonts->TexHeight > 0);
    const float inv_tex_w = 1.0f / float(io.Fonts->TexWidth);
    const float inv_tex_h = 1.0f / float(io.Fonts->TexHeight);
    const ImFontAtlasCustomRect* const rect = GetTextureCustomRect(icon);
    const ImVec2 size = { float(rect->Width), float(rect->Height) };
    const ImVec2 uv0 = ImVec2(float(rect->X) * inv_tex_w, float(rect->Y) * inv_tex_h);
    const ImVec2 uv1 = ImVec2(float(rect->X + rect->Width) * inv_tex_w, float(rect->Y + rect->Height) * inv_tex_h);
    ImGui::PushStyleColor(ImGuiCol_Button,        { 0.25f, 0.25f, 0.25f, 0.0f });
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {  0.4f, 0.4f,  0.4f,  highlight_on_hover ? 1.0f : 0.0f });
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  { 0.25f, 0.25f, 0.25f, highlight_on_hover ? 1.0f : 0.0f });
    const bool res = ImGuiPureWrap::image_button(tex_id, size, uv0, uv1);
    ImGui::PopStyleColor(3);

    if (!tooltip.empty() && ImGui::IsItemHovered())
        ImGuiPureWrap::tooltip(tooltip, ImGui::GetFontSize() * 20.0f);

    return res;
}

// It's a copy of IMGui::Selactable function.
// But a little beat modified to change a label text.
// If item is hovered we should use another color for highlighted letters.
// To do that we push a ColorMarkerHovered symbol at the very beginning of the label
// This symbol will be used to a color selection for the highlighted letters.
// see imgui_draw.cpp, void ImFont::RenderText()
static bool selectable(const char* label, bool selected, ImGuiSelectableFlags flags = 0, const ImVec2& size_arg = ImVec2(0, 0))
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    // Submit label or explicit size to ItemSize(), whereas ItemAdd() will submit a larger/spanning rectangle.
    ImGuiID id = window->GetID(label);
    ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    ImVec2 size(size_arg.x != 0.0f ? size_arg.x : label_size.x, size_arg.y != 0.0f ? size_arg.y : label_size.y);
    ImVec2 pos = window->DC.CursorPos;
    pos.y += window->DC.CurrLineTextBaseOffset;
    ImGui::ItemSize(size, 0.0f);

    // Fill horizontal space
    // We don't support (size < 0.0f) in Selectable() because the ItemSpacing extension would make explicitly right-aligned sizes not visibly match other widgets.
    const bool span_all_columns = (flags & ImGuiSelectableFlags_SpanAllColumns) != 0;
    const float min_x = span_all_columns ? window->ParentWorkRect.Min.x : pos.x;
    const float max_x = span_all_columns ? window->ParentWorkRect.Max.x : window->WorkRect.Max.x;
    if (size_arg.x == 0.0f || (flags & ImGuiSelectableFlags_SpanAvailWidth))
        size.x = ImMax(label_size.x, max_x - min_x);

    // Text stays at the submission position, but bounding box may be extended on both sides
    const ImVec2 text_min = pos;
    const ImVec2 text_max(min_x + size.x, pos.y + size.y);

    // Selectables are meant to be tightly packed together with no click-gap, so we extend their box to cover spacing between selectable.
    ImRect bb(min_x, pos.y, text_max.x, text_max.y);
    if ((flags & ImGuiSelectableFlags_NoPadWithHalfSpacing) == 0)
    {
        const float spacing_x = span_all_columns ? 0.0f : style.ItemSpacing.x;
        const float spacing_y = style.ItemSpacing.y;
        const float spacing_L = IM_FLOOR(spacing_x * 0.50f);
        const float spacing_U = IM_FLOOR(spacing_y * 0.50f);
        bb.Min.x -= spacing_L;
        bb.Min.y -= spacing_U;
        bb.Max.x += (spacing_x - spacing_L);
        bb.Max.y += (spacing_y - spacing_U);
    }
    //if (g.IO.KeyCtrl) { GetForegroundDrawList()->AddRect(bb.Min, bb.Max, IM_COL32(0, 255, 0, 255)); }

    // Modify ClipRect for the ItemAdd(), faster than doing a PushColumnsBackground/PushTableBackground for every Selectable..
    const float backup_clip_rect_min_x = window->ClipRect.Min.x;
    const float backup_clip_rect_max_x = window->ClipRect.Max.x;
    if (span_all_columns)
    {
        window->ClipRect.Min.x = window->ParentWorkRect.Min.x;
        window->ClipRect.Max.x = window->ParentWorkRect.Max.x;
    }

    bool item_add;
    if (flags & ImGuiSelectableFlags_Disabled)
    {
        ImGuiItemFlags backup_item_flags = g.CurrentItemFlags;
        g.CurrentItemFlags |= ImGuiItemFlags_Disabled | ImGuiItemFlags_NoNavDefaultFocus;
        item_add = ImGui::ItemAdd(bb, id);
        g.CurrentItemFlags = backup_item_flags;
    }
    else
    {
        item_add = ImGui::ItemAdd(bb, id);
    }

    if (span_all_columns)
    {
        window->ClipRect.Min.x = backup_clip_rect_min_x;
        window->ClipRect.Max.x = backup_clip_rect_max_x;
    }

    if (!item_add)
        return false;

    // FIXME: We can standardize the behavior of those two, we could also keep the fast path of override ClipRect + full push on render only,
    // which would be advantageous since most selectable are not selected.
    if (span_all_columns && window->DC.CurrentColumns)
        ImGui::PushColumnsBackground();
    else if (span_all_columns && g.CurrentTable)
        ImGui::TablePushBackgroundChannel();

    // We use NoHoldingActiveID on menus so user can click and _hold_ on a menu then drag to browse child entries
    ImGuiButtonFlags button_flags = 0;
    if (flags & ImGuiSelectableFlags_NoHoldingActiveID) { button_flags |= ImGuiButtonFlags_NoHoldingActiveId; }
    if (flags & ImGuiSelectableFlags_SelectOnClick)     { button_flags |= ImGuiButtonFlags_PressedOnClick; }
    if (flags & ImGuiSelectableFlags_SelectOnRelease)   { button_flags |= ImGuiButtonFlags_PressedOnRelease; }
    if (flags & ImGuiSelectableFlags_Disabled)          { button_flags |= ImGuiButtonFlags_Disabled; }
    if (flags & ImGuiSelectableFlags_AllowDoubleClick)  { button_flags |= ImGuiButtonFlags_PressedOnClickRelease | ImGuiButtonFlags_PressedOnDoubleClick; }
    if (flags & ImGuiSelectableFlags_AllowItemOverlap)  { button_flags |= ImGuiButtonFlags_AllowItemOverlap; }

    if (flags & ImGuiSelectableFlags_Disabled)
        selected = false;

    const bool was_selected = selected;
    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, button_flags);

    // Update NavId when clicking or when Hovering (this doesn't happen on most widgets), so navigation can be resumed with gamepad/keyboard
    if (pressed || (hovered && (flags & ImGuiSelectableFlags_SetNavIdOnHover)))
    {
        if (!g.NavDisableMouseHover && g.NavWindow == window && g.NavLayer == window->DC.NavLayerCurrent)
        {
            ImGui::SetNavID(id, window->DC.NavLayerCurrent, window->DC.NavFocusScopeIdCurrent, ImRect(bb.Min - window->Pos, bb.Max - window->Pos));
            g.NavDisableHighlight = true;
        }
    }
    if (pressed)
        ImGui::MarkItemEdited(id);

    if (flags & ImGuiSelectableFlags_AllowItemOverlap)
        ImGui::SetItemAllowOverlap();

    // In this branch, Selectable() cannot toggle the selection so this will never trigger.
    if (selected != was_selected) //-V547
        window->DC.LastItemStatusFlags |= ImGuiItemStatusFlags_ToggledSelection;

    // Render
    if (held && (flags & ImGuiSelectableFlags_DrawHoveredWhenHeld))
        hovered = true;
    if (hovered || selected)
    {
        const ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_HeaderActive : hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Header);
        ImGui::RenderFrame(bb.Min, bb.Max, col, false, 0.0f);
        ImGui::RenderNavHighlight(bb, id, ImGuiNavHighlightFlags_TypeThin | ImGuiNavHighlightFlags_NoRounding);
    }

    if (span_all_columns && window->DC.CurrentColumns)
        ImGui::PopColumnsBackground();
    else if (span_all_columns && g.CurrentTable)
        ImGui::TablePopBackgroundChannel();

    // mark a label with a ColorMarkerHovered, if item is hovered
    char marked_label[512]; //255 symbols is not enough for translated string (e.t. to Russian)
    if (hovered)
        sprintf(marked_label, "%c%s", ImGui::ColorMarkerHovered, label);
    else
        strcpy(marked_label, label);

    if (flags & ImGuiSelectableFlags_Disabled) ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
    ImGui::RenderTextClipped(text_min, text_max, marked_label, NULL, &label_size, style.SelectableTextAlign, &bb);
    if (flags & ImGuiSelectableFlags_Disabled) ImGui::PopStyleColor();

    // Automatically close popups
    if (pressed && (window->Flags & ImGuiWindowFlags_Popup) && !(flags & ImGuiSelectableFlags_DontClosePopups) && !(g.CurrentItemFlags & ImGuiItemFlags_SelectableDontClosePopup))
        ImGui::CloseCurrentPopup();

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, window->DC.LastItemStatusFlags);
    return pressed;
}

// Scroll so that the hovered item is at the top of the window
static void scroll_y(int hover_id)
{
    if (hover_id < 0)
        return;
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;

    float item_size_y = window->DC.PrevLineSize.y + g.Style.ItemSpacing.y;
    float item_delta = 0.5 * item_size_y;

    float item_top = item_size_y * hover_id;
    float item_bottom = item_top + item_size_y;

    float win_top = window->Scroll.y;
    float win_bottom = window->Scroll.y + window->Size.y;

    if (item_bottom + item_delta >= win_bottom)
        ImGui::SetScrollY(win_top + item_size_y);
    else if (item_top - item_delta <= win_top)
        ImGui::SetScrollY(win_top - item_size_y);
}

// Use this function instead of ImGui::IsKeyPressed.
// ImGui::IsKeyPressed is related for *GImGui.IO.KeysDownDuration[user_key_index]
// And after first key pressing IsKeyPressed() return "true" always even if key wasn't pressed
static void process_key_down(ImGuiKey imgui_key, std::function<void()> f)
{
    if (ImGui::IsKeyDown(ImGui::GetKeyIndex(imgui_key)))
    {
        f();
        // set KeysDown to false to avoid redundant key down processing
        ImGuiContext& g = *GImGui;
        g.IO.KeysDown[ImGui::GetKeyIndex(imgui_key)] = false;
    }
}

void ImGuiWrapper::search_list(const ImVec2& size_, bool (*items_getter)(int, const char** label, const char** tooltip), char* search_str,
                               Search::OptionViewParameters& view_params, int& selected, bool& edited, int& mouse_wheel, bool is_localized)
{
    int& hovered_id = view_params.hovered_id;
    // ImGui::ListBoxHeader("", size);
    {   
        // rewrote part of function to add a TextInput instead of label Text
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return ;

        const ImGuiStyle& style = g.Style;

        // Size default to hold ~7 items. Fractional number of items helps seeing that we can scroll down/up without looking at scrollbar.
        ImVec2 size = ImGui::CalcItemSize(size_, ImGui::CalcItemWidth(), ImGui::GetTextLineHeightWithSpacing() * 7.4f + style.ItemSpacing.y);
        ImRect frame_bb(window->DC.CursorPos, ImVec2(window->DC.CursorPos.x + size.x, window->DC.CursorPos.y + size.y));

        ImRect bb(frame_bb.Min, frame_bb.Max);
        window->DC.LastItemRect = bb; // Forward storage for ListBoxFooter.. dodgy.
        g.NextItemData.ClearFlags();

        if (!ImGui::IsRectVisible(bb.Min, bb.Max))
        {
            ImGui::ItemSize(bb.GetSize(), style.FramePadding.y);
            ImGui::ItemAdd(bb, 0, &frame_bb);
            return ;
        }

        ImGui::BeginGroup();

        const ImGuiID id = ImGui::GetID(search_str);
        ImVec2 search_size = ImVec2(size.x, ImGui::GetTextLineHeightWithSpacing() + style.ItemSpacing.y);

        if (!ImGui::IsAnyItemFocused() && !ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0))
            ImGui::SetKeyboardFocusHere(0);

        // The press on Esc key invokes editing of InputText (removes last changes)
        // So we should save previous value...
        std::string str = search_str;
        ImGui::InputTextEx("", NULL, search_str, 240, search_size, ImGuiInputTextFlags_AutoSelectAll, NULL, NULL);
        edited = ImGui::IsItemEdited();
        if (edited)
            hovered_id = 0;

        process_key_down(ImGuiKey_Escape, [&selected, search_str, str]() {
            // use 9999 to mark selection as a Esc key
            selected = 9999;
            // ... and when Esc key was pressed, than revert search_str value
            strcpy(search_str, str.c_str());
        });

        ImGui::BeginChildFrame(id, frame_bb.GetSize());
    }

    int i = 0;
    const char* item_text;
    const char* tooltip;
    int mouse_hovered = -1;

    while (items_getter(i, &item_text, &tooltip))
    {
        selectable(item_text, i == hovered_id);

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", /*item_text*/tooltip);
                hovered_id = -1;
            mouse_hovered = i;
        }

        if (ImGui::IsItemClicked())
            selected = i;
        i++;
    }

    // Process mouse wheel
    if (mouse_hovered > 0)
        ImGuiPureWrap::process_mouse_wheel(mouse_wheel);

    // process Up/DownArrows and Enter
    process_key_down(ImGuiKey_UpArrow, [&hovered_id, mouse_hovered]() {
        if (mouse_hovered > 0)
            ImGuiPureWrap::scroll_up();
        else {
            if (hovered_id > 0)
                --hovered_id;
            scroll_y(hovered_id);
        }
    });

    process_key_down(ImGuiKey_DownArrow, [&hovered_id, mouse_hovered, i]() {
        if (mouse_hovered > 0)
            ImGuiPureWrap::scroll_down();
        else {
            if (hovered_id < 0)
                hovered_id = 0;
            else if (hovered_id < i - 1)
                ++hovered_id;
            scroll_y(hovered_id);
        }
    });

    process_key_down(ImGuiKey_Enter, [&selected, hovered_id]() {
        selected = hovered_id;
    });

    ImGui::ListBoxFooter();

    auto check_box = [&edited](const std::string& label, bool& check) {
        ImGui::SameLine();
        bool ch = check;
        ImGuiPureWrap::checkbox(label, ch);
        if (ImGui::IsItemClicked()) {
            check = !check;
            edited = true;
        }
    };

    ImGui::AlignTextToFramePadding();

    // add checkboxes for show/hide Categories and Groups
    ImGuiPureWrap::text(_u8L("Use for search")+":");
    check_box(_u8L("Category"),   view_params.category);
    if (is_localized)
        check_box(_u8L("Search in English"), view_params.english);
}

void ImGuiWrapper::disabled_begin(bool disabled)
{
    wxCHECK_RET(!m_disabled, "ImGUI: Unbalanced disabled_begin() call");

    if (disabled) {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        m_disabled = true;
    }
}

void ImGuiWrapper::disabled_end()
{
    if (m_disabled) {
        ImGui::PopItemFlag();
        ImGui::PopStyleVar();
        m_disabled = false;
    }
}

ImFontAtlasCustomRect* ImGuiWrapper::GetTextureCustomRect(const wchar_t& tex_id)
{
    auto item = m_custom_glyph_rects_ids.find(tex_id);
    return (item != m_custom_glyph_rects_ids.end()) ? ImGui::GetIO().Fonts->GetCustomRectByIndex(m_custom_glyph_rects_ids[tex_id]) : nullptr;
}

ImU32 ImGuiPSWrap::to_ImU32(const ColorRGBA& color)
{
    return ImGui::GetColorU32({ color.r(), color.g(), color.b(), color.a() });
}

ImVec4 ImGuiPSWrap::to_ImVec4(const ColorRGBA& color)
{
    return { color.r(), color.g(), color.b(), color.a() };
}

ColorRGBA ImGuiPSWrap::from_ImU32(const ImU32& color)
{
    return from_ImVec4(ImGui::ColorConvertU32ToFloat4(color));
}

ColorRGBA ImGuiPSWrap::from_ImVec4(const ImVec4& color)
{
    return { color.x, color.y, color.z, color.w };
}

template <typename T, typename Func> 
static bool input_optional(std::optional<T> &v, Func& f, std::function<bool(const T&)> is_default, const T& def_val)
{
    if (v.has_value()) {
        if (f(*v)) {
            if (is_default(*v)) v.reset();
            return true;
        }
    } else {
        T val = def_val;
        if (f(val)) {
            if (!is_default(val)) v = val;
            return true;
        }
    }
    return false;
}

bool ImGuiWrapper::slider_optional_float(const char           *label,
                                         std::optional<float> &v,
                                         float                 v_min,
                                         float                 v_max,
                                         const char           *format,
                                         float                 power,
                                         bool                  clamp,
                                         const wxString       &tooltip,
                                         bool                  show_edit_btn,
                                         float                 def_val)
{
    auto func = [&](float &value) {
        return slider_float(label, &value, v_min, v_max, format, power, clamp, tooltip, show_edit_btn);
    };
    std::function<bool(const float &)> is_default =
        [def_val](const float &value) -> bool {
        return std::fabs(value - def_val) <= std::numeric_limits<float>::epsilon();
    };
    return input_optional(v, func, is_default, def_val);
}

bool ImGuiWrapper::slider_optional_int(const char         *label,
                                       std::optional<int> &v,
                                       int                 v_min,
                                       int                 v_max,
                                       const char         *format,
                                       float               power,
                                       bool                clamp,
                                       const wxString     &tooltip,
                                       bool                show_edit_btn,
                                       int                 def_val)
{
    std::optional<float> val;
    if (v.has_value()) val = static_cast<float>(*v);
    auto func = [&](float &value) {
        return slider_float(label, &value, v_min, v_max, format, power, clamp, tooltip, show_edit_btn);
    };
    std::function<bool(const float &)> is_default =
        [def_val](const float &value) -> bool {
        return std::fabs(value - def_val) < 0.9f;
    };

    float default_value = static_cast<float>(def_val);
    if (input_optional(val, func, is_default, default_value)) {
        if (val.has_value())
            v = static_cast<int>(std::round(*val));
        else
            v.reset(); 
        return true;
    } else return false;
}

ImVec2 ImGuiWrapper::suggest_location(const ImVec2 &dialog_size,
                                      const Slic3r::Polygon &interest,
                                      const ImVec2 &canvas_size)
{
    // IMPROVE 1: do not select place over menu
    // BoundingBox top_menu;
    // GLGizmosManager &gizmo_mng = canvas->get_gizmos_manager();
    // BoundingBox      side_menu; // gizmo_mng.get_size();
    // BoundingBox left_bottom_menu; // is permanent?
    // NotificationManager *notify_mng = plater->get_notification_manager();
    // BoundingBox          notifications; // notify_mng->get_size();
    // m_window_width, m_window_height + position

    // IMPROVE 2: use polygon of interest not only bounding box
    BoundingBox bb(interest.points);
    Point       center = bb.center(); // interest.centroid();

    // area size
    Point window_center(canvas_size.x / 2, canvas_size.y / 2);

    // mov on side
    Point bb_half_size = (bb.max - bb.min) / 2 + Point(1,1);
    Point diff_center  = window_center - center;
    Vec2d diff_norm(diff_center.x() / (double) bb_half_size.x(),
                    diff_center.y() / (double) bb_half_size.y());
    if (diff_norm.x() > 1.) diff_norm.x() = 1.;
    if (diff_norm.x() < -1.) diff_norm.x() = -1.;
    if (diff_norm.y() > 1.) diff_norm.y() = 1.;
    if (diff_norm.y() < -1.) diff_norm.y() = -1.;

    Vec2d abs_diff(abs(diff_norm.x()), abs(diff_norm.y()));
    if (abs_diff.x() < 1. && abs_diff.y() < 1.) {
        if (abs_diff.x() > abs_diff.y())
            diff_norm.x() = (diff_norm.x() < 0.) ? (-1.) : 1.;
        else
            diff_norm.y() = (diff_norm.y() < 0.) ? (-1.) : 1.;
    }

    Point half_dialog_size(dialog_size.x / 2., dialog_size.y / 2.);
    Point move_size       = bb_half_size + half_dialog_size;
    Point offseted_center = center - half_dialog_size;
    Vec2d offset(offseted_center.x() + diff_norm.x() * move_size.x(),
                 offseted_center.y() + diff_norm.y() * move_size.y());

    // move offset close to center
    Points window_polygon = {offset.cast<int>(),
                             Point(offset.x(), offset.y() + dialog_size.y),
                             Point(offset.x() + dialog_size.x,
                                   offset.y() + dialog_size.y),
                             Point(offset.x() + dialog_size.x, offset.y())};
    // check that position by Bounding box is not intersecting
    assert(Slic3r::intersection(interest, Polygon(window_polygon)).empty());

    double allowed_space = 10; // in px
    double allowed_space_sq = allowed_space * allowed_space;
    Vec2d  move_vec         = (center - (offset.cast<int>() + half_dialog_size))
                         .cast<double>();    
    Vec2d result_move(0, 0);
    do {
        move_vec             = move_vec / 2.;
        Point  move_point    = (move_vec + result_move).cast<int>();
        Points moved_polygon = window_polygon; // copy
        for (Point &p : moved_polygon) p += move_point;
        if (Slic3r::intersection(interest, Polygon(moved_polygon)).empty())
            result_move += move_vec;
        
    } while (move_vec.squaredNorm() >= allowed_space_sq);
    offset += result_move;

    return ImVec2(offset.x(), offset.y());
}

void ImGuiWrapper::draw(
    const Polygon &polygon,
    ImDrawList *   draw_list /* = ImGui::GetOverlayDrawList()*/,
    ImU32          color     /* = ImGui::GetColorU32(COL_ORANGE_LIGHT)*/,
    float          thickness /* = 3.f*/)
{
    // minimal one line consist of 2 points
    if (polygon.size() < 2) return;
    // need a place to draw
    if (draw_list == nullptr) return;

    const Point *prev_point = &polygon.points.back();
    for (const Point &point : polygon.points) {
        ImVec2 p1(prev_point->x(), prev_point->y());
        ImVec2 p2(point.x(), point.y());
        draw_list->AddLine(p1, p2, color, thickness);
        prev_point = &point;
    }
}

#ifdef __APPLE__
static const ImWchar ranges_keyboard_shortcuts[] =
{
    0x21E7, 0x21E7, // OSX Shift Key symbol
    0x2318, 0x2318, // OSX Command Key symbol
    0x2325, 0x2325, // OSX Option Key symbol
    0,
};
#endif // __APPLE__


std::vector<unsigned char> ImGuiWrapper::load_svg(const std::string& bitmap_name, unsigned target_width, unsigned target_height)
{
    std::vector<unsigned char> empty_vector;

    NSVGimage* image = BitmapCache::nsvgParseFromFileWithReplace(Slic3r::var(bitmap_name + ".svg").c_str(), "px", 96.0f, { { "\"#808080\"", "\"#FFFFFF\"" } });
    if (image == nullptr)
        return empty_vector;

    float svg_scale = target_height != 0 ?
        (float)target_height / image->height : target_width != 0 ?
        (float)target_width / image->width : 1;

    int   width = (int)(svg_scale * image->width + 0.5f);
    int   height = (int)(svg_scale * image->height + 0.5f);
    int   n_pixels = width * height;
    if (n_pixels <= 0) {
        ::nsvgDelete(image);
        return empty_vector;
    }

    NSVGrasterizer* rast = ::nsvgCreateRasterizer();
    if (rast == nullptr) {
        ::nsvgDelete(image);
        return empty_vector;
    }

    std::vector<unsigned char> data(n_pixels * 4, 0);
    ::nsvgRasterize(rast, image, 0, 0, svg_scale, data.data(), width, height, width * 4);
    ::nsvgDeleteRasterizer(rast);
    ::nsvgDelete(image);

    return data;
}

void ImGuiWrapper::init_font(bool compress)
{
    destroy_font();

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    // Create ranges of characters from m_glyph_ranges, possibly adding some OS specific special characters.
    ImVector<ImWchar> ranges;
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(m_glyph_ranges);

    builder.AddChar(ImWchar(0x2026)); // …

    if (s_font_cjk) {
        // https://github.com/prusa3d/PrusaSlicer/issues/8171: The translation
        // contains characters not in the ImGui ranges for simplified Chinese. Add them manually.
        // This should no longer be needed because the following block would add them automatically.
        builder.AddChar(ImWchar(0x5ED3));
        builder.AddChar(ImWchar(0x8F91));
    }

    // Add the characters that that needed the fallback character.
    for (ImWchar c : s_missing_chars) {
        builder.AddChar(c);
        s_fixed_chars.emplace(c);
    }
    s_missing_chars.clear();

#ifdef __APPLE__
	if (s_font_cjk)
		// Apple keyboard shortcuts are only contained in the CJK fonts.
		builder.AddRanges(ranges_keyboard_shortcuts);
#endif
	builder.BuildRanges(&ranges); // Build the final result (ordered ranges with all the unique characters submitted)

    //FIXME replace with io.Fonts->AddFontFromMemoryTTF(buf_decompressed_data, (int)buf_decompressed_size, m_font_size, nullptr, ranges.Data);
    //https://github.com/ocornut/imgui/issues/220
    ImFont* font = io.Fonts->AddFontFromFileTTF((Slic3r::resources_dir() + "/fonts/" + "NotoSans-Regular.ttf").c_str(), m_font_size, nullptr, ranges.Data);
    if (s_font_cjk) {
        ImFontConfig config;
        config.MergeMode = true;
        io.Fonts->AddFontFromFileTTF((Slic3r::resources_dir() + "/fonts/" + "NotoSansCJK-Regular.ttc").c_str(), m_font_size, &config, ranges.Data);
    }
    
    if (font == nullptr) {
        font = io.Fonts->AddFontDefault();
        if (font == nullptr) {
            throw Slic3r::RuntimeError("ImGui: Could not load deafult font");
        }
    }

    float font_scale = m_font_size/15;
    int icon_sz = lround(16 * font_scale); // default size of icon is 16 px

    int rect_id = io.Fonts->CustomRects.Size;  // id of the rectangle added next
    // add rectangles for the icons to the font atlas
    for (auto& icon : font_icons) {
        m_custom_glyph_rects_ids[icon.first] =
            io.Fonts->AddCustomRectFontGlyph(font, icon.first, icon_sz, icon_sz, 3.0 * font_scale + icon_sz);
    }
    const int icon_sz_m = int(1.25 * icon_sz); // default size of medium icon is 20 px
    for (auto& icon : font_icons_medium) {
        m_custom_glyph_rects_ids[icon.first] =
            io.Fonts->AddCustomRectFontGlyph(font, icon.first, icon_sz_m, icon_sz_m, 3.0 * font_scale + icon_sz_m);
    }
    for (auto& icon : font_icons_large) {
        m_custom_glyph_rects_ids[icon.first] =
            io.Fonts->AddCustomRectFontGlyph(font, icon.first, icon_sz * 2, icon_sz * 2, 3.0 * font_scale + icon_sz * 2);
    }
    for (auto& icon : font_icons_extra_large) {
        m_custom_glyph_rects_ids[icon.first] =
            io.Fonts->AddCustomRectFontGlyph(font, icon.first, icon_sz * 4, icon_sz * 4, 3.0 * font_scale + icon_sz * 4);
    }

    // Build texture atlas
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);   // Load as RGBA 32-bits (75% of the memory is wasted, but default font is so small) because it is more likely to be compatible with user's existing shaders. If your ImTextureId represent a higher-level concept than just a GL texture id, consider calling GetTexDataAsAlpha8() instead to save on GPU memory.

    auto load_icon_from_svg = [this, &io, pixels, width, &rect_id](const std::pair<const wchar_t, std::string> icon, int icon_sz) {
        if (const ImFontAtlas::CustomRect* rect = io.Fonts->GetCustomRectByIndex(rect_id)) {
            assert(rect->Width == icon_sz);
            assert(rect->Height == icon_sz);
            std::vector<unsigned char> raw_data = load_svg(icon.second, icon_sz, icon_sz);
            if (!raw_data.empty()) {
                const ImU32* pIn = (ImU32*)raw_data.data();
                for (int y = 0; y < icon_sz; y++) {
                    ImU32* pOut = (ImU32*)pixels + (rect->Y + y) * width + (rect->X);
                    for (int x = 0; x < icon_sz; x++)
                        *pOut++ = *pIn++;
                }
            }
        }
        rect_id++;
    };

    // Fill rectangles from the SVG-icons
    for (auto icon : font_icons) {
        load_icon_from_svg(icon, icon_sz);
    }

    for (auto icon : font_icons_medium) {
        load_icon_from_svg(icon, icon_sz_m);
    }

    icon_sz *= 2; // default size of large icon is 32 px
    for (auto icon : font_icons_large) {
        load_icon_from_svg(icon, icon_sz);
    }

    icon_sz *= 2; // default size of extra large icon is 64 px
    for (auto icon : font_icons_extra_large) {
        load_icon_from_svg(icon, icon_sz);
    }

    // Upload texture to graphics system
    GLint last_texture;
    glsafe(::glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
    glsafe(::glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture));
    glsafe(::glGenTextures(1, &m_font_texture));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_font_texture));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    glsafe(::glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
    if (compress && OpenGLManager::are_compressed_textures_supported())
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels));
    else
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels));

    // Store our identifier
    io.Fonts->TexID = (ImTextureID)(intptr_t)m_font_texture;

    // Restore state
    glsafe(::glBindTexture(GL_TEXTURE_2D, last_texture));
}

void ImGuiWrapper::init_input()
{
    ImGuiIO& io = ImGui::GetIO();

    // Keyboard mapping. ImGui will use those indices to peek into the io.KeysDown[] array.
    io.KeyMap[ImGuiKey_Tab] = WXK_TAB;
    io.KeyMap[ImGuiKey_LeftArrow] = WXK_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = WXK_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow] = WXK_UP;
    io.KeyMap[ImGuiKey_DownArrow] = WXK_DOWN;
    io.KeyMap[ImGuiKey_PageUp] = WXK_PAGEUP;
    io.KeyMap[ImGuiKey_PageDown] = WXK_PAGEDOWN;
    io.KeyMap[ImGuiKey_Home] = WXK_HOME;
    io.KeyMap[ImGuiKey_End] = WXK_END;
    io.KeyMap[ImGuiKey_Insert] = WXK_INSERT;
    io.KeyMap[ImGuiKey_Delete] = WXK_DELETE;
    io.KeyMap[ImGuiKey_Backspace] = WXK_BACK;
    io.KeyMap[ImGuiKey_Space] = WXK_SPACE;
    io.KeyMap[ImGuiKey_Enter] = WXK_RETURN;
    io.KeyMap[ImGuiKey_KeyPadEnter] = WXK_NUMPAD_ENTER;
    io.KeyMap[ImGuiKey_Escape] = WXK_ESCAPE;
    io.KeyMap[ImGuiKey_A] = 'A';
    io.KeyMap[ImGuiKey_C] = 'C';
    io.KeyMap[ImGuiKey_V] = 'V';
    io.KeyMap[ImGuiKey_X] = 'X';
    io.KeyMap[ImGuiKey_Y] = 'Y';
    io.KeyMap[ImGuiKey_Z] = 'Z';

    // Don't let imgui special-case Mac, wxWidgets already do that
    io.ConfigMacOSXBehaviors = false;

    // Setup clipboard interaction callbacks
    io.SetClipboardTextFn = clipboard_set;
    io.GetClipboardTextFn = clipboard_get;
    io.ClipboardUserData = this;
}

void ImGuiWrapper::init_style()
{
    ImGuiStyle &style = ImGui::GetStyle();

    auto set_color = [&](ImGuiCol_ entity, ImVec4 color) {
        style.Colors[entity] = color;
    };

    // Window
    style.WindowRounding = 4.0f;
    set_color(ImGuiCol_WindowBg,        ImGuiPureWrap::COL_WINDOW_BACKGROUND);
    set_color(ImGuiCol_TitleBgActive,   ImGuiPureWrap::COL_ORANGE_DARK);

    // Generics
    set_color(ImGuiCol_FrameBg,         ImGuiPureWrap::COL_GREY_DARK);
    set_color(ImGuiCol_FrameBgHovered,  ImGuiPureWrap::COL_GREY_LIGHT);
    set_color(ImGuiCol_FrameBgActive,   ImGuiPureWrap::COL_GREY_LIGHT);

    // Text selection
    set_color(ImGuiCol_TextSelectedBg,  ImGuiPureWrap::COL_ORANGE_DARK);

    // Buttons
    set_color(ImGuiCol_Button,          ImGuiPureWrap::COL_BUTTON_BACKGROUND);
    set_color(ImGuiCol_ButtonHovered,   ImGuiPureWrap::COL_BUTTON_HOVERED);
    set_color(ImGuiCol_ButtonActive,    ImGuiPureWrap::COL_BUTTON_ACTIVE);

    // Checkbox
    set_color(ImGuiCol_CheckMark,       ImGuiPureWrap::COL_ORANGE_LIGHT);

    // ComboBox items
    set_color(ImGuiCol_Header,          ImGuiPureWrap::COL_ORANGE_DARK);
    set_color(ImGuiCol_HeaderHovered,   ImGuiPureWrap::COL_ORANGE_LIGHT);
    set_color(ImGuiCol_HeaderActive,    ImGuiPureWrap::COL_ORANGE_LIGHT);

    // Slider
    set_color(ImGuiCol_SliderGrab,      ImGuiPureWrap::COL_ORANGE_DARK);
    set_color(ImGuiCol_SliderGrabActive,ImGuiPureWrap::COL_ORANGE_LIGHT);

    // Separator
    set_color(ImGuiCol_Separator,       ImGuiPureWrap::COL_ORANGE_LIGHT);

    // Tabs
    set_color(ImGuiCol_Tab,                 ImGuiPureWrap::COL_ORANGE_DARK);
    set_color(ImGuiCol_TabHovered,          ImGuiPureWrap::COL_ORANGE_LIGHT);
    set_color(ImGuiCol_TabActive,           ImGuiPureWrap::COL_ORANGE_LIGHT);
    set_color(ImGuiCol_TabUnfocused,        ImGuiPureWrap::COL_GREY_DARK);
    set_color(ImGuiCol_TabUnfocusedActive,  ImGuiPureWrap::COL_GREY_LIGHT);

    // Scrollbars
    set_color(ImGuiCol_ScrollbarGrab,       ImGuiPureWrap::COL_ORANGE_DARK);
    set_color(ImGuiCol_ScrollbarGrabHovered,ImGuiPureWrap::COL_ORANGE_LIGHT);
    set_color(ImGuiCol_ScrollbarGrabActive, ImGuiPureWrap::COL_ORANGE_LIGHT);
}

void ImGuiWrapper::render_draw_data(ImDrawData *draw_data)
{
    if (draw_data == nullptr || draw_data->CmdListsCount == 0)
        return;

    GLShaderProgram* shader = wxGetApp().get_shader("imgui");
    if (shader == nullptr)
        return;

    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    ImGuiIO& io = ImGui::GetIO();
    const int fb_width  = (int)(draw_data->DisplaySize.x * io.DisplayFramebufferScale.x);
    const int fb_height = (int)(draw_data->DisplaySize.y * io.DisplayFramebufferScale.y);
    if (fb_width == 0 || fb_height == 0)
        return;

    GLShaderProgram* curr_shader = wxGetApp().get_current_shader();
    if (curr_shader != nullptr)
        curr_shader->stop_using();

    shader->start_using();

    // Backup GL state
    GLenum last_active_texture;       glsafe(::glGetIntegerv(GL_ACTIVE_TEXTURE, (GLint*)&last_active_texture));
    GLuint last_program;              glsafe(::glGetIntegerv(GL_CURRENT_PROGRAM, (GLint*)&last_program));
    GLuint last_texture;              glsafe(::glGetIntegerv(GL_TEXTURE_BINDING_2D, (GLint*)&last_texture));
    GLuint last_array_buffer;         glsafe(::glGetIntegerv(GL_ARRAY_BUFFER_BINDING, (GLint*)&last_array_buffer));
    GLuint last_vertex_array_object = 0;
#if !SLIC3R_OPENGL_ES
    if (OpenGLManager::get_gl_info().is_core_profile())
#endif // !SLIC3R_OPENGL_ES
        glsafe(::glGetIntegerv(GL_VERTEX_ARRAY_BINDING, (GLint*)&last_vertex_array_object));
    GLint last_viewport[4];           glsafe(::glGetIntegerv(GL_VIEWPORT, last_viewport));
    GLint last_scissor_box[4];        glsafe(::glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box));
    GLenum last_blend_src_rgb;        glsafe(::glGetIntegerv(GL_BLEND_SRC_RGB, (GLint*)&last_blend_src_rgb));
    GLenum last_blend_dst_rgb;        glsafe(::glGetIntegerv(GL_BLEND_DST_RGB, (GLint*)&last_blend_dst_rgb));
    GLenum last_blend_src_alpha;      glsafe(::glGetIntegerv(GL_BLEND_SRC_ALPHA, (GLint*)&last_blend_src_alpha));
    GLenum last_blend_dst_alpha;      glsafe(::glGetIntegerv(GL_BLEND_DST_ALPHA, (GLint*)&last_blend_dst_alpha));
    GLenum last_blend_equation_rgb;   glsafe(::glGetIntegerv(GL_BLEND_EQUATION_RGB, (GLint*)&last_blend_equation_rgb));
    GLenum last_blend_equation_alpha; glsafe(::glGetIntegerv(GL_BLEND_EQUATION_ALPHA, (GLint*)&last_blend_equation_alpha));
    GLboolean last_enable_blend        = ::glIsEnabled(GL_BLEND);
    GLboolean last_enable_cull_face    = ::glIsEnabled(GL_CULL_FACE);
    GLboolean last_enable_depth_test   = ::glIsEnabled(GL_DEPTH_TEST);
    GLboolean last_enable_stencil_test = ::glIsEnabled(GL_STENCIL_TEST);
    GLboolean last_enable_scissor_test = ::glIsEnabled(GL_SCISSOR_TEST);

    // set new GL state
    glsafe(::glActiveTexture(GL_TEXTURE0));
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendEquation(GL_FUNC_ADD));
    glsafe(::glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_STENCIL_TEST));
    glsafe(::glEnable(GL_SCISSOR_TEST));

    // Setup viewport, orthographic projection matrix
    // Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
    glsafe(::glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height));
    const float L = draw_data->DisplayPos.x;
    const float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    const float T = draw_data->DisplayPos.y;
    const float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;

    Matrix4f ortho_projection;
    ortho_projection <<
        2.0f / (R - L), 0.0f,           0.0f,  (R + L) / (L - R),
        0.0f,           2.0f / (T - B), 0.0f,  (T + B) / (B - T),
        0.0f,           0.0f,           -1.0f, 0.0f,
        0.0f,           0.0f,           0.0f,  1.0f;

    shader->set_uniform("Texture", 0);
    shader->set_uniform("ProjMtx", ortho_projection);

    // Will project scissor/clipping rectangles into framebuffer space
    const ImVec2 clip_off   = draw_data->DisplayPos;       // (0,0) unless using multi-viewports
    const ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

    // Render command lists
    for (int n = 0; n < draw_data->CmdListsCount; ++n) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        const ImDrawVert* vtx_buffer = cmd_list->VtxBuffer.Data;
        const ImDrawIdx* idx_buffer  = cmd_list->IdxBuffer.Data;
        const GLsizeiptr vtx_buffer_size = (GLsizeiptr)cmd_list->VtxBuffer.Size * (int)sizeof(ImDrawVert);
        const GLsizeiptr idx_buffer_size = (GLsizeiptr)cmd_list->IdxBuffer.Size * (int)sizeof(ImDrawIdx);

        GLuint vao_id = 0;
#if !SLIC3R_OPENGL_ES
        if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif // !SLIC3R_OPENGL_ES
            glsafe(::glGenVertexArrays(1, &vao_id));
            glsafe(::glBindVertexArray(vao_id));
#if !SLIC3R_OPENGL_ES
        }
#endif // !SLIC3R_OPENGL_ES

        GLuint vbo_id;
        glsafe(::glGenBuffers(1, &vbo_id));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, vbo_id));
        glsafe(::glBufferData(GL_ARRAY_BUFFER, vtx_buffer_size, vtx_buffer, GL_STATIC_DRAW));

        const int position_id = shader->get_attrib_location("Position");
        if (position_id != -1) {
            glsafe(::glVertexAttribPointer(position_id, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (const void*)IM_OFFSETOF(ImDrawVert, pos)));
            glsafe(::glEnableVertexAttribArray(position_id));
        }
        const int uv_id = shader->get_attrib_location("UV");
        if (uv_id != -1) {
            glsafe(::glVertexAttribPointer(uv_id, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (const void*)IM_OFFSETOF(ImDrawVert, uv)));
            glsafe(::glEnableVertexAttribArray(uv_id));
        }
        const int color_id = shader->get_attrib_location("Color");
        if (color_id != -1) {
            glsafe(::glVertexAttribPointer(color_id, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (const void*)IM_OFFSETOF(ImDrawVert, col)));
            glsafe(::glEnableVertexAttribArray(color_id));
        }

        GLuint ibo_id;
        glsafe(::glGenBuffers(1, &ibo_id));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_id));
        glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx_buffer_size, idx_buffer, GL_STATIC_DRAW));

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; ++cmd_i) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback)
                // User callback (registered via ImDrawList::AddCallback)
                pcmd->UserCallback(cmd_list, pcmd);
            else {
                // Project scissor/clipping rectangles into framebuffer space
                const ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                const ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                // Apply scissor/clipping rectangle (Y is inverted in OpenGL)
                glsafe(::glScissor((int)clip_min.x, (int)(fb_height - clip_max.y), (int)(clip_max.x - clip_min.x), (int)(clip_max.y - clip_min.y)));

                // Bind texture, Draw
                glsafe(::glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->GetTexID()));
                glsafe(::glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, (void*)(intptr_t)(pcmd->IdxOffset * sizeof(ImDrawIdx))));
            }
        }

        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

        if (color_id != -1)
            glsafe(::glDisableVertexAttribArray(color_id));
        if (uv_id != -1)
            glsafe(::glDisableVertexAttribArray(uv_id));
        if (position_id != -1)
            glsafe(::glDisableVertexAttribArray(position_id));

        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));

        glsafe(::glDeleteBuffers(1, &ibo_id));
        glsafe(::glDeleteBuffers(1, &vbo_id));
#if !SLIC3R_OPENGL_ES
        if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif // !SLIC3R_OPENGL_ES
            if (vao_id > 0)
                glsafe(::glDeleteVertexArrays(1, &vao_id));
#if !SLIC3R_OPENGL_ES
        }
#endif // !SLIC3R_OPENGL_ES
    }

    // Restore modified GL state
    glsafe(::glBindTexture(GL_TEXTURE_2D, last_texture));
    glsafe(::glActiveTexture(last_active_texture));
#if !SLIC3R_OPENGL_ES
    if (OpenGLManager::get_gl_info().is_core_profile())
#endif // !SLIC3R_OPENGL_ES
        glsafe(::glBindVertexArray(last_vertex_array_object));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer));
    glsafe(::glBlendEquationSeparate(last_blend_equation_rgb, last_blend_equation_alpha));
    glsafe(::glBlendFuncSeparate(last_blend_src_rgb, last_blend_dst_rgb, last_blend_src_alpha, last_blend_dst_alpha));
    if (last_enable_blend) glsafe(::glEnable(GL_BLEND)); else glsafe(::glDisable(GL_BLEND));
    if (last_enable_cull_face) glsafe(::glEnable(GL_CULL_FACE)); else glsafe(::glDisable(GL_CULL_FACE));
    if (last_enable_depth_test) glsafe(::glEnable(GL_DEPTH_TEST)); else glsafe(::glDisable(GL_DEPTH_TEST));
    if (last_enable_stencil_test) glsafe(::glEnable(GL_STENCIL_TEST)); else glsafe(::glDisable(GL_STENCIL_TEST));
    if (last_enable_scissor_test) glsafe(::glEnable(GL_SCISSOR_TEST)); else glsafe(::glDisable(GL_SCISSOR_TEST));
    glsafe(::glViewport(last_viewport[0], last_viewport[1], (GLsizei)last_viewport[2], (GLsizei)last_viewport[3]));
    glsafe(::glScissor(last_scissor_box[0], last_scissor_box[1], (GLsizei)last_scissor_box[2], (GLsizei)last_scissor_box[3]));

    shader->stop_using();

    if (curr_shader != nullptr)
        curr_shader->start_using();
}

bool ImGuiWrapper::display_initialized() const
{
    const ImGuiIO& io = ImGui::GetIO();
    return io.DisplaySize.x >= 0.0f && io.DisplaySize.y >= 0.0f;
}

void ImGuiWrapper::destroy_font()
{
    if (m_font_texture != 0) {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->TexID = 0;
        glsafe(::glDeleteTextures(1, &m_font_texture));
        m_font_texture = 0;

        // We have destroyed current font, including all characters that we may have added dynamically.
        // Move move all characters that we already added into the list of missing chars again,
        // so they are all added at once.
        for (ImWchar c : s_fixed_chars)
            s_missing_chars.emplace(c);
        s_fixed_chars.clear();
    }
}

const char* ImGuiWrapper::clipboard_get(void* user_data)
{
    ImGuiWrapper *self = reinterpret_cast<ImGuiWrapper*>(user_data);

    const char* res = "";

    if (wxTheClipboard->Open()) {
        if (wxTheClipboard->IsSupported(wxDF_TEXT)
#if wxUSE_UNICODE
        || wxTheClipboard->IsSupported(wxDF_UNICODETEXT)
#endif // wxUSE_UNICODE
            ) {
            wxTextDataObject data;
            wxTheClipboard->GetData(data);

            if (data.GetTextLength() > 0) {
                self->m_clipboard_text = into_u8(data.GetText());
                res = self->m_clipboard_text.c_str();
            }
        }

        wxTheClipboard->Close();
    }

    return res;
}

void ImGuiWrapper::clipboard_set(void* /* user_data */, const char* text)
{
    if (wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(text)));   // object owned by the clipboard
        wxTheClipboard->Close();
    }
}



} // namespace GUI
} // namespace Slic3r
