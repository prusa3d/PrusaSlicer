///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef ARRANGESETTINGSDIALOGIMGUI_HPP
#define ARRANGESETTINGSDIALOGIMGUI_HPP

#include <arrange-wrapper/ArrangeSettingsView.hpp>
#include "ImGuiWrapper.hpp"
#include "libslic3r/AnyPtr.hpp"

namespace Slic3r {
namespace GUI {

class ArrangeSettingsDialogImgui: public arr2::ArrangeSettingsView {
    ImGuiWrapper *m_imgui;
    AnyPtr<arr2::ArrangeSettingsDb> m_db;

    std::function<void()> m_on_arrange_btn;
    std::function<void()> m_on_arrange_bed_btn;
    std::function<void()> m_on_reset_btn;

    std::function<bool()> m_show_xl_combo_predicate = [] { return true; };

public:
    ArrangeSettingsDialogImgui(ImGuiWrapper *imgui, AnyPtr<arr2::ArrangeSettingsDb> db);

    void render(float pos_x, float pos_y, bool current_bed);

    void show_xl_align_combo(std::function<bool()> pred)
    {
        m_show_xl_combo_predicate = pred;
    }

    void on_arrange_btn(std::function<void()> on_arrangefn)
    {
        m_on_arrange_btn = on_arrangefn;
    }

    void on_arrange_bed_btn(std::function<void()> on_arrangefn)
    {
        m_on_arrange_bed_btn = on_arrangefn;
    }

    void on_reset_btn(std::function<void()> on_resetfn)
    {
        m_on_reset_btn = on_resetfn;
    }

    // ArrangeSettingsView iface:

    float get_distance_from_objects() const override { return m_db->get_distance_from_objects(); }
    float get_distance_from_bed() const  override { return m_db->get_distance_from_bed(); }
    bool  is_rotation_enabled() const override { return m_db->is_rotation_enabled(); }

    XLPivots get_xl_alignment() const override { return m_db->get_xl_alignment(); }
    GeometryHandling get_geometry_handling() const override { return m_db->get_geometry_handling(); }
    ArrangeStrategy get_arrange_strategy() const override { return arr2::ArrangeSettingsView::asAuto; }
};

}} // namespace Slic3r::GUI

#endif // ARRANGESETTINGSDIALOGIMGUI_HPP
