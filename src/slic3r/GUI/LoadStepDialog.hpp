///|/ Copyright (c) Prusa Research 2018 - 2025 Oleksandra Iushchenko @YuSanka
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/

#ifndef slic3r_LoadStepDialog_hpp_
#define slic3r_LoadStepDialog_hpp_

#include <string>
#include <wx/dialog.h>
#include "GUI_Utils.hpp"

class wxBoxSizer;
class wxTextCtrl;
class wxSlider;
class CheckBox;

namespace Slic3r::GUI {

struct PrecisionParams
{
    double linear;
    double angle;
};

struct SliderHelper
{
    double min_val;
    double max_val;
    double val_step;
    int beg_sl_pos;
    int end_sl_pos;

    void init(double min, double max, double step, int beg_pos = 1) {
        assert(val_step != 0.);
        min_val = min;
        max_val = max;
        val_step = step;

        beg_sl_pos = beg_pos;
        end_sl_pos = beg_sl_pos + int(double(max_val - min_val) / val_step);
    }

    double get_value(int pos) const {
        return max_val - val_step * (pos - beg_sl_pos);
    }

    int get_pos(double value) const {
        return beg_sl_pos + int((max_val - value) / val_step);
    }

    double adjust_to_region(double value) const {
        return std::max(std::min(value, max_val), min_val);
    }
};

class LoadStepDialog : public DPIDialog
{
public:
    LoadStepDialog(wxWindow* parent, const std::string& filename, double linear_precision, double angle_precision, bool multiple_loading);
    ~LoadStepDialog() = default;

    bool IsCheckBoxChecked();
    bool IsApplyToAllClicked();

    double get_linear_precision() { return m_params.linear; }
    double get_angle_precision() { return m_params.angle; }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override {}
    void on_sys_color_changed() override {};


private:
    void add_params(wxSizer* sizer);
    void enable_customs(bool enable);

private:
    PrecisionParams     m_params;

    ::CheckBox*         m_remember_chb              { nullptr };

    wxTextCtrl*         m_linear_precision_val      { nullptr };
    wxTextCtrl*         m_angle_precision_val       { nullptr };

    wxSlider*           m_linear_precision_slider   { nullptr };
    wxSlider*           m_angle_precision_slider    { nullptr };

    wxBoxSizer*         m_custom_sizer              { nullptr };

    bool                m_default                   { false };
    bool                m_apply_to_all              { false };

    SliderHelper        m_linear_precision_sl;
    SliderHelper        m_angle_precision_sl;
};

}    // namespace Slic3r::GUI

#endif
