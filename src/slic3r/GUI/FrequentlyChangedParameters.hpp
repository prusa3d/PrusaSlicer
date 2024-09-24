///|/ Copyright (c) Prusa Research 2018 - 2023 Tomáš Mészáros @tamasmeszaros, Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, David Kocík @kocikdav, Lukáš Hejl @hejllukas, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Pavel Mikuš @Godrak, Filip Sykala @Jony01, Vojtěch Král @vojtechkral
///|/
///|/ ported from lib/Slic3r/GUI/Plater.pm:
///|/ Copyright (c) Prusa Research 2016 - 2019 Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral, Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Tomáš Mészáros @tamasmeszaros
///|/ Copyright (c) 2018 Martin Loidl @LoidlM
///|/ Copyright (c) 2017 Matthias Gazzari @qtux
///|/ Copyright (c) Slic3r 2012 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2017 Joseph Lenox @lordofhyphens
///|/ Copyright (c) 2015 Daren Schwenke
///|/ Copyright (c) 2014 Mark Hindess
///|/ Copyright (c) 2012 Mike Sheldrake @mesheldrake
///|/ Copyright (c) 2012 Henrik Brix Andersen @henrikbrixandersen
///|/ Copyright (c) 2012 Sam Wong
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_FreqChangedParams_hpp_
#define slic3r_FreqChangedParams_hpp_

#include <memory>

#include "Event.hpp"

class wxButton;
class wxSizer;
class wxWindow;
class ScalableButton;

namespace Slic3r {

namespace GUI {

class ConfigOptionsGroup;

class FreqChangedParams
{
    double		    m_brim_width = 0.0;
    wxButton*       m_wiping_dialog_button{ nullptr };
    wxSizer*        m_sizer {nullptr};

    std::shared_ptr<ConfigOptionsGroup> m_og_fff;
    std::shared_ptr<ConfigOptionsGroup> m_og_sla;

    std::vector<ScalableButton*>        m_empty_buttons;

public:

    FreqChangedParams(wxWindow* parent);
    ~FreqChangedParams() = default;

    wxButton*       get_wiping_dialog_button() noexcept { return m_wiping_dialog_button; }
    wxSizer*        get_sizer() noexcept                { return m_sizer; }
    void            Show(bool is_fff) const;

    ConfigOptionsGroup* get_og(bool is_fff);

    void            msw_rescale();
    void            sys_color_changed();
};

} // namespace GUI
} // namespace Slic3r

#endif
