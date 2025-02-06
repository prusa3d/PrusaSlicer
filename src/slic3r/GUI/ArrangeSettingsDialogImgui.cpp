///|/ Copyright (c) Prusa Research 2023 Oleksandra Iushchenko @YuSanka, Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "ArrangeSettingsDialogImgui.hpp"
#include "I18N.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/GUI.hpp"

namespace Slic3r { namespace GUI {

struct Settings {
    float d_obj;
    float d_bed;
    bool  rotations;
    int   xl_align;
    int   geom_handling;
    int   arr_strategy;
};

static void read_settings(Settings &s, const arr2::ArrangeSettingsDb *db)
{
    assert(db);
    s.d_obj = db->get_distance_from_objects();
    s.d_bed = db->get_distance_from_bed();
    s.rotations = db->is_rotation_enabled();
    s.xl_align  = db->get_xl_alignment();
    s.geom_handling = db->get_geometry_handling();
    s.arr_strategy = db->get_arrange_strategy();
}

ArrangeSettingsDialogImgui::ArrangeSettingsDialogImgui(
    ImGuiWrapper *imgui, AnyPtr<arr2::ArrangeSettingsDb> db)
    : m_imgui{imgui}, m_db{std::move(db)}
{}

void ArrangeSettingsDialogImgui::render(float pos_x, float pos_y, bool current_bed)
{
    assert(m_imgui && m_db);

    ImGuiPureWrap::set_next_window_pos(pos_x, pos_y, ImGuiCond_Always, 0.5f, 0.0f);

    ImGuiPureWrap::begin(_u8L("Arrange options"),
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoCollapse);

    Settings settings;
    read_settings(settings, m_db.get());

    ImGuiPureWrap::text(GUI::format(
        _L("Press %1%left mouse button to enter the exact value"),
        shortkey_ctrl_prefix()));

    float dobj_min, dobj_max;
    float dbed_min, dbed_max;

    m_db->distance_from_obj_range(dobj_min, dobj_max);
    m_db->distance_from_bed_range(dbed_min, dbed_max);

    if(dobj_min > settings.d_obj) {
        settings.d_obj = std::max(dobj_min, settings.d_obj);
        m_db->set_distance_from_objects(settings.d_obj);
    }

    if (dbed_min > settings.d_bed) {
        settings.d_bed = std::max(dbed_min, settings.d_bed);
        m_db->set_distance_from_bed(settings.d_bed);
    }

    if (m_imgui->slider_float(_L("Spacing"), &settings.d_obj, dobj_min,
                              dobj_max, "%5.2f")) {
        settings.d_obj = std::max(dobj_min, settings.d_obj);
        m_db->set_distance_from_objects(settings.d_obj);
    }

    if (m_imgui->slider_float(_L("Spacing from bed"), &settings.d_bed,
                              dbed_min, dbed_max, "%5.2f")) {
        settings.d_bed = std::max(dbed_min, settings.d_bed);
        m_db->set_distance_from_bed(settings.d_bed);
    }

    if (ImGuiPureWrap::checkbox(_u8L("Enable rotations (slow)"), settings.rotations)) {
        m_db->set_rotation_enabled(settings.rotations);
    }

    if (m_show_xl_combo_predicate() &&
        settings.xl_align >= 0 &&
        ImGuiPureWrap::combo(_u8L("Alignment"),
                       {_u8L("Center"), _u8L("Rear left"), _u8L("Front left"),
                        _u8L("Front right"), _u8L("Rear right"),
                        _u8L("Random")},
                       settings.xl_align)) {
        if (settings.xl_align >= 0 &&
            settings.xl_align < ArrangeSettingsView::xlpCount)
            m_db->set_xl_alignment(static_cast<ArrangeSettingsView::XLPivots>(
                settings.xl_align));
    }

    // TRN ArrangeDialog
    if (ImGuiPureWrap::combo(_u8L("Geometry handling"),
        // TRN ArrangeDialog: Type of "Geometry handling"
         {_u8L("Fast"),
        // TRN ArrangeDialog: Type of "Geometry handling"
          _u8L("Balanced"),
        // TRN ArrangeDialog: Type of "Geometry handling"
          _u8L("Accurate")},
                       settings.geom_handling)) {
        if (settings.geom_handling >= 0 &&
            settings.geom_handling < ArrangeSettingsView::ghCount)
            m_db->set_geometry_handling(
                static_cast<ArrangeSettingsView::GeometryHandling>(
                    settings.geom_handling));
    }

    ImGui::Separator();

    if (ImGuiPureWrap::button(_u8L("Reset defaults"))) {
        arr2::ArrangeSettingsDb::Values df =  m_db->get_defaults();
        m_db->set_distance_from_objects(df.d_obj);
        m_db->set_distance_from_bed(df.d_bed);
        m_db->set_rotation_enabled(df.rotations);
        if (m_show_xl_combo_predicate())
            m_db->set_xl_alignment(df.xl_align);

        m_db->set_geometry_handling(df.geom_handling);
        m_db->set_arrange_strategy(df.arr_strategy);

        if (m_on_reset_btn)
            m_on_reset_btn();
    }

    ImGui::SameLine();

    if (!current_bed && ImGuiPureWrap::button(_u8L("Arrange")) && m_on_arrange_btn) {
        m_on_arrange_btn();
    }
    if (current_bed && ImGuiPureWrap::button(_u8L("Arrange bed")) && m_on_arrange_bed_btn) {
        m_on_arrange_bed_btn();
    }

    ImGuiPureWrap::end();
}

}} // namespace Slic3r::GUI
