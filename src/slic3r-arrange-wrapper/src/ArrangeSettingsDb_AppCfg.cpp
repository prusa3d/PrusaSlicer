///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <arrange-wrapper/ArrangeSettingsDb_AppCfg.hpp>

#include <LocalesUtils.hpp>
#include <libslic3r/AppConfig.hpp>

#include <arrange-wrapper/ArrangeSettingsView.hpp>

namespace Slic3r {

ArrangeSettingsDb_AppCfg::ArrangeSettingsDb_AppCfg(AppConfig *appcfg) : m_appcfg{appcfg}
{
    sync();
}

void ArrangeSettingsDb_AppCfg::sync()
{
    m_settings_fff.postfix = "_fff";
    m_settings_fff_seq.postfix = "_fff_seq_print";
    m_settings_sla.postfix = "_sla";

    std::string dist_fff_str =
        m_appcfg->get("arrange", "min_object_distance_fff");

    std::string dist_bed_fff_str =
        m_appcfg->get("arrange", "min_bed_distance_fff");

    std::string dist_fff_seq_print_str =
        m_appcfg->get("arrange", "min_object_distance_fff_seq_print");

    std::string dist_bed_fff_seq_print_str =
        m_appcfg->get("arrange", "min_bed_distance_fff_seq_print");

    std::string dist_sla_str =
        m_appcfg->get("arrange", "min_object_distance_sla");

    std::string dist_bed_sla_str =
        m_appcfg->get("arrange", "min_bed_distance_sla");

    std::string en_rot_fff_str =
        m_appcfg->get("arrange", "enable_rotation_fff");

    std::string en_rot_fff_seqp_str =
        m_appcfg->get("arrange", "enable_rotation_fff_seq_print");

    std::string en_rot_sla_str =
        m_appcfg->get("arrange", "enable_rotation_sla");

    std::string alignment_xl_str =
        m_appcfg->get("arrange", "alignment_xl");

    std::string geom_handling_str =
        m_appcfg->get("arrange", "geometry_handling");

    std::string strategy_str =
        m_appcfg->get("arrange", "arrange_strategy");

    if (!dist_fff_str.empty())
        m_settings_fff.vals.d_obj = string_to_float_decimal_point(dist_fff_str);
    else
        m_settings_fff.vals.d_obj = m_settings_fff.defaults.d_obj;

    if (!dist_bed_fff_str.empty())
        m_settings_fff.vals.d_bed = string_to_float_decimal_point(dist_bed_fff_str);
    else
        m_settings_fff.vals.d_bed = m_settings_fff.defaults.d_bed;

    if (!dist_fff_seq_print_str.empty())
        m_settings_fff_seq.vals.d_obj = string_to_float_decimal_point(dist_fff_seq_print_str);
    else
        m_settings_fff_seq.vals.d_obj = m_settings_fff_seq.defaults.d_obj;

    if (!dist_bed_fff_seq_print_str.empty())
        m_settings_fff_seq.vals.d_bed = string_to_float_decimal_point(dist_bed_fff_seq_print_str);
    else
        m_settings_fff_seq.vals.d_bed = m_settings_fff_seq.defaults.d_bed;

    if (!dist_sla_str.empty())
        m_settings_sla.vals.d_obj = string_to_float_decimal_point(dist_sla_str);
    else
        m_settings_sla.vals.d_obj = m_settings_sla.defaults.d_obj;

    if (!dist_bed_sla_str.empty())
        m_settings_sla.vals.d_bed = string_to_float_decimal_point(dist_bed_sla_str);
    else
        m_settings_sla.vals.d_bed = m_settings_sla.defaults.d_bed;

    if (!en_rot_fff_str.empty())
        m_settings_fff.vals.rotations = (en_rot_fff_str == "1" || en_rot_fff_str == "yes");

    if (!en_rot_fff_seqp_str.empty())
        m_settings_fff_seq.vals.rotations = (en_rot_fff_seqp_str == "1" || en_rot_fff_seqp_str == "yes");
    else
        m_settings_fff_seq.vals.rotations = m_settings_fff_seq.defaults.rotations;

    if (!en_rot_sla_str.empty())
        m_settings_sla.vals.rotations = (en_rot_sla_str == "1" || en_rot_sla_str == "yes");
    else
        m_settings_sla.vals.rotations = m_settings_sla.defaults.rotations;

    // Override default alignment and save/load it to a temporary slot "alignment_xl"
    auto arr_alignment = ArrangeSettingsView::to_xl_pivots(alignment_xl_str)
                             .value_or(m_settings_fff.defaults.xl_align);

    m_settings_sla.vals.xl_align = arr_alignment ;
    m_settings_fff.vals.xl_align = arr_alignment ;
    m_settings_fff_seq.vals.xl_align = arr_alignment ;

    auto geom_handl = ArrangeSettingsView::to_geometry_handling(geom_handling_str)
                          .value_or(m_settings_fff.defaults.geom_handling);

    m_settings_sla.vals.geom_handling = geom_handl;
    m_settings_fff.vals.geom_handling = geom_handl;
    m_settings_fff_seq.vals.geom_handling = geom_handl;

    auto arr_strategy = ArrangeSettingsView::to_arrange_strategy(strategy_str)
                            .value_or(m_settings_fff.defaults.arr_strategy);

    m_settings_sla.vals.arr_strategy = arr_strategy;
    m_settings_fff.vals.arr_strategy = arr_strategy;
    m_settings_fff_seq.vals.arr_strategy = arr_strategy;
}

void ArrangeSettingsDb_AppCfg::distance_from_obj_range(float &min,
                                                       float &max) const
{
    min = get_slot(this).dobj_range.minval;
    max = get_slot(this).dobj_range.maxval;
}

void ArrangeSettingsDb_AppCfg::distance_from_bed_range(float &min,
                                                       float &max) const
{
    min = get_slot(this).dbed_range.minval;
    max = get_slot(this).dbed_range.maxval;
}

arr2::ArrangeSettingsDb& ArrangeSettingsDb_AppCfg::set_distance_from_objects(float v)
{
    Slot &slot = get_slot(this);
    slot.vals.d_obj = v;
    m_appcfg->set("arrange", "min_object_distance" + slot.postfix,
                  float_to_string_decimal_point(v));

    return *this;
}

arr2::ArrangeSettingsDb& ArrangeSettingsDb_AppCfg::set_distance_from_bed(float v)
{
    Slot &slot = get_slot(this);
    slot.vals.d_bed = v;
    m_appcfg->set("arrange", "min_bed_distance" + slot.postfix,
                  float_to_string_decimal_point(v));

    return *this;
}

arr2::ArrangeSettingsDb& ArrangeSettingsDb_AppCfg::set_rotation_enabled(bool v)
{
    Slot &slot = get_slot(this);
    slot.vals.rotations = v;
    m_appcfg->set("arrange", "enable_rotation" + slot.postfix, v ? "1" : "0");

    return *this;
}

arr2::ArrangeSettingsDb& ArrangeSettingsDb_AppCfg::set_xl_alignment(XLPivots v)
{
    m_settings_fff.vals.xl_align = v;
    m_appcfg->set("arrange", "alignment_xl", std::string{get_label(v)});

    return *this;
}

arr2::ArrangeSettingsDb& ArrangeSettingsDb_AppCfg::set_geometry_handling(GeometryHandling v)
{
    m_settings_fff.vals.geom_handling = v;
    m_appcfg->set("arrange", "geometry_handling", std::string{get_label(v)});

    return *this;
}

arr2::ArrangeSettingsDb& ArrangeSettingsDb_AppCfg::set_arrange_strategy(ArrangeStrategy v)
{
    m_settings_fff.vals.arr_strategy = v;
    m_appcfg->set("arrange", "arrange_strategy", std::string{get_label(v)});

    return *this;
}

} // namespace Slic3r
