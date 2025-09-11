///|/ Copyright (c) Prusa Research 2018 - 2023 Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Vojtěch Bubník @bubnikv, David Kocík @kocikdav, Lukáš Matěna @lukasmatena, Tomáš Mészáros @tamasmeszaros, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2020 Benjamin Greiner
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "wxExtensions.hpp"

#include <stdexcept>
#include <cmath>

#include <wx/sizer.h>
#include <wx/accel.h>

#include <boost/algorithm/string/replace.hpp>
#include <boost/log/trivial.hpp>

#include "BitmapCache.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "I18N.hpp"
#include "GUI_Utils.hpp"
#include "Plater.hpp"
#include "../Utils/MacDarkMode.hpp"
#include "BitmapComboBox.hpp"
#include "libslic3r/Utils.hpp"
#include "OG_CustomCtrl.hpp"
#include "format.hpp"

#include "libslic3r/Color.hpp"

#ifndef __linux__
// msw_menuitem_bitmaps is used for MSW and OSX
static std::map<int, std::string> msw_menuitem_bitmaps;
void sys_color_changed_menu(wxMenu* menu)
{
	struct update_icons {
		static void run(wxMenuItem* item) {
			const auto it = msw_menuitem_bitmaps.find(item->GetId());
			if (it != msw_menuitem_bitmaps.end()) {
				wxBitmapBundle* item_icon = get_bmp_bundle(it->second);
				if (item_icon->IsOk())
					item->SetBitmap(*item_icon);
			}
			if (item->IsSubMenu())
				for (wxMenuItem *sub_item : item->GetSubMenu()->GetMenuItems())
					update_icons::run(sub_item);
		}
	};

	for (wxMenuItem *item : menu->GetMenuItems())
		update_icons::run(item);
}
#endif /* no __linux__ */

#ifndef __APPLE__
std::vector<wxAcceleratorEntry*>& accelerator_entries_cache()
{
    static std::vector<wxAcceleratorEntry*> entries;
    return entries;
}
#endif

void enable_menu_item(wxUpdateUIEvent& evt, std::function<bool()> const cb_condition, wxMenuItem* item, wxWindow* win)
{
    const bool enable = cb_condition();
    evt.Enable(enable);
}

wxMenuItem* append_menu_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, wxBitmapBundle* icon, wxEvtHandler* event_handler,
    std::function<bool()> const cb_condition, wxWindow* parent, int insert_pos/* = wxNOT_FOUND*/)
{
    if (id == wxID_ANY)
        id = wxNewId();

    auto *item = new wxMenuItem(menu, id, string, description);
    if (icon && icon->IsOk()) {
        item->SetBitmap(*icon);
    }
    if (insert_pos == wxNOT_FOUND)
        menu->Append(item);
    else
        menu->Insert(insert_pos, item);

#ifdef __WXMSW__
    if (event_handler != nullptr && event_handler != menu)
        event_handler->Bind(wxEVT_MENU, cb, id);
    else
#endif // __WXMSW__
#ifndef __APPLE__
    if (parent)
        parent->Bind(wxEVT_MENU, cb, id);
    else
#endif // n__APPLE__
        menu->Bind(wxEVT_MENU, cb, id);

#ifndef __APPLE__
    if (wxAcceleratorEntry* entry = wxAcceleratorEntry::Create(string)) {

        static const std::set<int> special_keys = {
                                                WXK_PAGEUP,
                                                WXK_PAGEDOWN,
                                                WXK_NUMPAD_PAGEDOWN,
                                                WXK_END,
                                                WXK_HOME,
                                                WXK_LEFT,
                                                WXK_UP,
                                                WXK_RIGHT,
                                                WXK_DOWN,
                                                WXK_INSERT,
                                                WXK_DELETE,
        };

        // Check for special keys not included in the table
        // see wxMSWKeyboard::WXWORD WXToVK(int wxk, bool *isExtended)
        if (std::find(special_keys.begin(), special_keys.end(), entry->GetKeyCode()) == special_keys.end()) {
            entry->SetMenuItem(item);
            accelerator_entries_cache().push_back(entry);
        }
        else {
            delete entry;
            entry = nullptr;
        }
    }
#endif

    if (parent) {
        parent->Bind(wxEVT_UPDATE_UI, [cb_condition, item, parent](wxUpdateUIEvent& evt) {
            enable_menu_item(evt, cb_condition, item, parent); }, id);
    }

    return item;
}

wxMenuItem* append_menu_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, const std::string& icon, wxEvtHandler* event_handler,
    std::function<bool()> const cb_condition, wxWindow* parent, int insert_pos/* = wxNOT_FOUND*/)
{
    if (id == wxID_ANY)
        id = wxNewId();

    wxBitmapBundle* bmp = icon.empty() ? nullptr : get_bmp_bundle(icon);

#ifndef __linux__
    if (bmp && bmp->IsOk())
        msw_menuitem_bitmaps[id] = icon;
#endif /* no __linux__ */

    return append_menu_item(menu, id, string, description, cb, bmp, event_handler, cb_condition, parent, insert_pos);
}

wxMenuItem* append_submenu(wxMenu* menu, wxMenu* sub_menu, int id, const wxString& string, const wxString& description, const std::string& icon,
    std::function<bool()> const cb_condition, wxWindow* parent)
{
    if (id == wxID_ANY)
        id = wxNewId();

    wxMenuItem* item = new wxMenuItem(menu, id, string, description);
    if (!icon.empty()) {
        item->SetBitmap(*get_bmp_bundle(icon));

#ifndef __linux__
        msw_menuitem_bitmaps[id] = icon;
#endif // no __linux__
    }

    item->SetSubMenu(sub_menu);
    menu->Append(item);

    if (parent) {
        parent->Bind(wxEVT_UPDATE_UI, [cb_condition, item, parent](wxUpdateUIEvent& evt) {
            enable_menu_item(evt, cb_condition, item, parent); }, id);
    }

    return item;
}

wxMenuItem* append_menu_radio_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, wxEvtHandler* event_handler)
{
    if (id == wxID_ANY)
        id = wxNewId();

    wxMenuItem* item = menu->AppendRadioItem(id, string, description);

#ifdef __WXMSW__
    if (event_handler != nullptr && event_handler != menu)
        event_handler->Bind(wxEVT_MENU, cb, id);
    else
#endif // __WXMSW__
        menu->Bind(wxEVT_MENU, cb, id);

    return item;
}

wxMenuItem* append_menu_check_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent & event)> cb, wxEvtHandler* event_handler,
    std::function<bool()> const enable_condition, std::function<bool()> const check_condition, wxWindow* parent)
{
    if (id == wxID_ANY)
        id = wxNewId();

    wxMenuItem* item = menu->AppendCheckItem(id, string, description);

#ifdef __WXMSW__
    if (event_handler != nullptr && event_handler != menu)
        event_handler->Bind(wxEVT_MENU, cb, id);
    else
#endif // __WXMSW__
        menu->Bind(wxEVT_MENU, cb, id);

    if (parent)
        parent->Bind(wxEVT_UPDATE_UI, [enable_condition, check_condition](wxUpdateUIEvent& evt)
            {
                evt.Enable(enable_condition());
                evt.Check(check_condition());
            }, id);

    return item;
}

void set_menu_item_bitmap(wxMenuItem* item, const std::string& icon_name)
{
    item->SetBitmap(*get_bmp_bundle(icon_name));
#ifndef __linux__
    const auto it = msw_menuitem_bitmaps.find(item->GetId());
    if (it != msw_menuitem_bitmaps.end() && it->second != icon_name)
        it->second = icon_name;
#endif // !__linux__
}

/* Function for rescale of buttons in Dialog under MSW if dpi is changed.
 * btn_ids - vector of buttons identifiers
 */
void msw_buttons_rescale(wxDialog* dlg, const int em_unit, const std::vector<int>& btn_ids, double height_koef/* = 1.*/)
{
    const wxSize& btn_size = wxSize(-1, int(2.5 * em_unit * height_koef + 0.5f));

    for (int btn_id : btn_ids) {
        // There is a case [FirmwareDialog], when we have wxControl instead of wxButton
        // so let casting everything to the wxControl
        wxControl* btn = static_cast<wxControl*>(dlg->FindWindowById(btn_id, dlg));
        if (btn)
            btn->SetMinSize(btn_size);
    }
}

/* Function for getting of em_unit value from correct parent.
 * In most of cases it is m_em_unit value from GUI_App,
 * but for DPIDialogs it's its own value. 
 * This value will be used to correct rescale after moving between 
 * Displays with different HDPI */
int em_unit(wxWindow* win)
{
    if (win)
    {
        wxTopLevelWindow *toplevel = Slic3r::GUI::find_toplevel_parent(win);
        Slic3r::GUI::DPIDialog* dlg = dynamic_cast<Slic3r::GUI::DPIDialog*>(toplevel);
        if (dlg)
            return dlg->em_unit();
        Slic3r::GUI::DPIFrame* frame = dynamic_cast<Slic3r::GUI::DPIFrame*>(toplevel);
        if (frame)
            return frame->em_unit();
    }
    
    return Slic3r::GUI::wxGetApp().em_unit();
}

int mode_icon_px_size()
{
#ifdef __APPLE__
    return 10;
#else
    return 12;
#endif
}

#ifdef __WXGTK2__
static int scale() 
{
    return int(em_unit(nullptr) * 0.1f + 0.5f);
}
#endif // __WXGTK2__

wxBitmapBundle* get_bmp_bundle(const std::string& bmp_name_in, int width/* = 16*/, int height/* = -1*/, const std::string& new_color/* = std::string()*/)
{
#ifdef __WXGTK2__
    width *= scale();
    if (height > 0)
        height *= scale();
#endif // __WXGTK2__

    static Slic3r::GUI::BitmapCache cache;

    std::string bmp_name = bmp_name_in;
    boost::replace_last(bmp_name, ".png", "");

    if (height < 0)
        height = width;

    // Try loading an SVG first, then PNG if SVG is not found:
    wxBitmapBundle* bmp = cache.from_svg(bmp_name, width, height, Slic3r::GUI::wxGetApp().dark_mode(), new_color);
    if (bmp == nullptr) {
        bmp = cache.from_png(bmp_name, width, height);
        if (!bmp)
            // Neither SVG nor PNG has been found, raise error
            throw Slic3r::RuntimeError("Could not load bitmap: " + bmp_name);
    }
    return bmp;
}

wxBitmapBundle* get_empty_bmp_bundle(int width, int height)
{
    static Slic3r::GUI::BitmapCache cache;
#ifdef __WXGTK2__
    return cache.mkclear_bndl(width * scale(), height * scale());
#else
    return cache.mkclear_bndl(width, height);
#endif // __WXGTK2__
}

wxBitmapBundle* get_solid_bmp_bundle(int width, int height, const std::string& color )
{
    static Slic3r::GUI::BitmapCache cache;
#ifdef __WXGTK2__
    return cache.mksolid_bndl(width * scale(), height * scale(), color, 1, Slic3r::GUI::wxGetApp().dark_mode());
#else
    return cache.mksolid_bndl(width, height, color, 1, Slic3r::GUI::wxGetApp().dark_mode());
#endif // __WXGTK2__
}

std::vector<wxBitmapBundle*> get_extruder_color_icons(bool thin_icon/* = false*/)
{
    // Create the bitmap with color bars.
    std::vector<wxBitmapBundle*> bmps;
    std::vector<std::string> colors = Slic3r::GUI::wxGetApp().plater()->get_extruder_color_strings_from_plater_config();

    if (colors.empty())
        return bmps;

    for (const std::string& color : colors)
        bmps.emplace_back(get_solid_bmp_bundle(thin_icon ? 16 : 32, 16, color));

    return bmps;
}


void apply_extruder_selector(Slic3r::GUI::BitmapComboBox** ctrl, 
                             wxWindow* parent,
                             const std::string& first_item/* = ""*/, 
                             wxPoint pos/* = wxDefaultPosition*/,
                             wxSize size/* = wxDefaultSize*/,
                             bool use_thin_icon/* = false*/)
{
    std::vector<wxBitmapBundle*> icons = get_extruder_color_icons(use_thin_icon);

    if (!*ctrl) {
        *ctrl = new Slic3r::GUI::BitmapComboBox(parent, wxID_ANY, wxEmptyString, pos, size, 0, nullptr, wxCB_READONLY);
        Slic3r::GUI::wxGetApp().UpdateDarkUI(*ctrl);
    }
    else
    {
        (*ctrl)->SetPosition(pos);
        (*ctrl)->SetMinSize(size);
        (*ctrl)->SetSize(size);
        (*ctrl)->Clear();
    }
    if (first_item.empty())
        (*ctrl)->Hide();    // to avoid unwanted rendering before layout (ExtruderSequenceDialog)

    if (icons.empty() && !first_item.empty()) {
        (*ctrl)->Append(_(first_item), wxNullBitmap);
        return;
    }

    // For ObjectList we use short extruder name (just a number)
    const bool use_full_item_name = dynamic_cast<Slic3r::GUI::ObjectList*>(parent) == nullptr;

    int i = 0;
    wxString str = _(L("Extruder"));
    for (wxBitmapBundle* bmp : icons) {
        if (i == 0) {
            if (!first_item.empty())
                (*ctrl)->Append(_(first_item), *bmp);
            ++i;
        }

        (*ctrl)->Append(use_full_item_name
                        ? Slic3r::GUI::from_u8((boost::format("%1% %2%") % str % i).str())
                        : wxString::Format("%d", i), *bmp);
        ++i;
    }
    (*ctrl)->SetSelection(0);
}


// ----------------------------------------------------------------------------
// LockButton
// ----------------------------------------------------------------------------

LockButton::LockButton( wxWindow *parent, 
                        wxWindowID id, 
                        const wxPoint& pos /*= wxDefaultPosition*/, 
                        const wxSize& size /*= wxDefaultSize*/):
                        wxButton(parent, id, wxEmptyString, pos, size, wxBU_EXACTFIT | wxNO_BORDER)
{
    m_bmp_lock_closed   = ScalableBitmap(this, "lock_closed");
    m_bmp_lock_closed_f = ScalableBitmap(this, "lock_closed_f");
    m_bmp_lock_open     = ScalableBitmap(this, "lock_open");
    m_bmp_lock_open_f   = ScalableBitmap(this, "lock_open_f");

    Slic3r::GUI::wxGetApp().UpdateDarkUI(this);
    SetBitmap(m_bmp_lock_open.bmp());
    SetBitmapDisabled(m_bmp_lock_open.bmp());
    SetBitmapCurrent(m_bmp_lock_closed_f.bmp());

    //button events
    Bind(wxEVT_BUTTON, &LockButton::OnButton, this);
}

void LockButton::OnButton(wxCommandEvent& event)
{
    if (m_disabled)
        return;

    SetLock(!m_is_pushed);
    event.Skip();
}

void LockButton::SetLock(bool lock)
{
    if (m_is_pushed != lock) {
        m_is_pushed = lock;
        update_button_bitmaps();
    }
}

void LockButton::sys_color_changed()
{
    Slic3r::GUI::wxGetApp().UpdateDarkUI(this);

    m_bmp_lock_closed.sys_color_changed();
    m_bmp_lock_closed_f.sys_color_changed();
    m_bmp_lock_open.sys_color_changed();
    m_bmp_lock_open_f.sys_color_changed();

    update_button_bitmaps();
}

void LockButton::update_button_bitmaps()
{
    SetBitmap(m_is_pushed ? m_bmp_lock_closed.bmp() : m_bmp_lock_open.bmp());
    SetBitmapCurrent(m_is_pushed ? m_bmp_lock_closed_f.bmp() : m_bmp_lock_open_f.bmp());

    Refresh();
    Update();
}

// ----------------------------------------------------------------------------
// PrusaBitmap
// ----------------------------------------------------------------------------
ScalableBitmap::ScalableBitmap( wxWindow *parent, 
                                const std::string& icon_name,
                                const int  width/* = 16*/,
                                const int  height/* = -1*/,
                                const bool grayscale/* = false*/):
    m_parent(parent), m_icon_name(icon_name),
    m_bmp_width(width), m_bmp_height(height)
{
    m_bmp = *get_bmp_bundle(icon_name, width, height);
    m_bitmap = m_bmp.GetBitmapFor(m_parent);
}

ScalableBitmap::ScalableBitmap( wxWindow*           parent,
                                const std::string&  icon_name,
                                const wxSize        icon_size,
                                const bool          grayscale/* = false*/) :
ScalableBitmap(parent, icon_name, icon_size.x, icon_size.y, grayscale)
{
}

ScalableBitmap::ScalableBitmap(wxWindow* parent, boost::filesystem::path& icon_path, const wxSize icon_size)
    :m_parent(parent), m_bmp_width(icon_size.x), m_bmp_height(icon_size.y)
{
    wxString path = Slic3r::GUI::from_u8(icon_path.string());
    wxBitmap bitmap;
    const std::string ext = icon_path.extension().string();

    if (ext == ".png" || ext == ".jpg") {
        if (!bitmap.LoadFile(path, ext == ".png" ? wxBITMAP_TYPE_PNG : wxBITMAP_TYPE_JPEG)) {
            BOOST_LOG_TRIVIAL(error) << "Failed to load bitmap " << path;
            return;
        }
        
        // check if the bitmap has a square shape

        if (wxSize sz = bitmap.GetSize(); sz.x != sz.y) {
            const int bmp_side = std::min(sz.GetWidth(), sz.GetHeight());

            wxRect rc = sz.GetWidth() > sz.GetHeight() ?
                        wxRect(int( 0.5 * (sz.x - sz.y)), 0, bmp_side, bmp_side) :
                        wxRect(0, int( 0.5 * (sz.y - sz.x)), bmp_side, bmp_side) ;

            bitmap = bitmap.GetSubBitmap(rc);
        }

        // set mask for circle shape

        wxBitmapBundle mask_bmps = *get_bmp_bundle("user_mask", bitmap.GetSize().GetWidth());
        wxMask* mask = new wxMask(mask_bmps.GetBitmap(bitmap.GetSize()), *wxBLACK);
        bitmap.SetMask(mask);

        // get allowed scale factors

        std::set<double> scales = { 1.0 };
#ifdef __APPLE__
        scales.emplace(Slic3r::GUI::mac_max_scaling_factor());
#elif _WIN32
        size_t disp_cnt = wxDisplay::GetCount();
        for (size_t disp = 0; disp < disp_cnt; ++disp)
            scales.emplace(wxDisplay(disp).GetScaleFactor());
#endif

        // create bitmaps for bundle

        wxVector<wxBitmap> bmps;
        for (double scale : scales) {
            wxBitmap bmp = bitmap;
            wxBitmap::Rescale(bmp, icon_size * scale);
            bmps.push_back(bmp);
        }
        m_bmp = wxBitmapBundle::FromBitmaps(bmps);
    }
    else if (ext == ".svg") {
        m_bmp = wxBitmapBundle::FromSVGFile(path, icon_size);
    }
}

void ScalableBitmap::sys_color_changed()
{
    m_bmp = *get_bmp_bundle(m_icon_name, m_bmp_width, m_bmp_height);
}

// ----------------------------------------------------------------------------
// PrusaButton
// ----------------------------------------------------------------------------

ScalableButton::ScalableButton( wxWindow *          parent,
                                wxWindowID          id,
                                const std::string&  icon_name /*= ""*/,
                                const wxString&     label /* = wxEmptyString*/,
                                const wxSize&       size /* = wxDefaultSize*/,
                                const wxPoint&      pos /* = wxDefaultPosition*/,
                                long                style /*= wxBU_EXACTFIT | wxNO_BORDER*/,
                                int                 width/* = 16*/, 
                                int                 height/* = -1*/) :
    m_parent(parent),
    m_current_icon_name(icon_name),
    m_bmp_width(width),
    m_bmp_height(height),
    m_has_border(!(style & wxNO_BORDER))
{
    Create(parent, id, label, pos, size, style);
    Slic3r::GUI::wxGetApp().UpdateDarkUI(this);

    if (!icon_name.empty()) {
        SetBitmap(*get_bmp_bundle(icon_name, width, height));
        if (!label.empty())
            SetBitmapMargins(int(0.5* em_unit(parent)), 0);
    }

    if (size != wxDefaultSize)
    {
        const int em = em_unit(parent);
        m_width = size.x/em;
        m_height= size.y/em;
    }
}


ScalableButton::ScalableButton( wxWindow *          parent, 
                                wxWindowID          id,
                                const ScalableBitmap&  bitmap,
                                const wxString&     label /*= wxEmptyString*/, 
                                long                style /*= wxBU_EXACTFIT | wxNO_BORDER*/) :
    m_parent(parent),
    m_current_icon_name(bitmap.name()),
    m_bmp_width(bitmap.px_size().x),
    m_bmp_height(bitmap.px_size().y),
    m_has_border(!(style& wxNO_BORDER))
{
    Create(parent, id, label, wxDefaultPosition, wxDefaultSize, style);
    Slic3r::GUI::wxGetApp().UpdateDarkUI(this);

    SetBitmap(bitmap.bmp());
}

void ScalableButton::SetBitmap_(const ScalableBitmap& bitmap)
{
    const wxBitmapBundle& bmp = bitmap.bmp();
    SetBitmap(bmp);
    SetBitmapCurrent(bmp);
    SetBitmapPressed(bmp);
    SetBitmapFocus(bmp);
    SetBitmapDisabled(bmp);
    m_current_icon_name = bitmap.name();
}

bool ScalableButton::SetBitmap_(const std::string& bmp_name)
{
    m_current_icon_name = bmp_name;
    if (m_current_icon_name.empty())
        return false;

    wxBitmapBundle bmp = *get_bmp_bundle(m_current_icon_name, m_bmp_width, m_bmp_height);
    SetBitmap(bmp);
    SetBitmapCurrent(bmp);
    SetBitmapPressed(bmp);
    SetBitmapFocus(bmp);
    SetBitmapDisabled(bmp);
    return true;
}

void ScalableButton::SetBitmapDisabled_(const ScalableBitmap& bmp)
{
    SetBitmapDisabled(bmp.bmp());
    m_disabled_icon_name = bmp.name();
}

int ScalableButton::GetBitmapHeight()
{
#ifdef __APPLE__
    return GetBitmap().GetScaledHeight();
#else
    return GetBitmap().GetHeight();
#endif
}

wxSize ScalableButton::GetBitmapSize()
{
#ifdef __APPLE__
    return wxSize(GetBitmap().GetScaledWidth(), GetBitmap().GetScaledHeight());
#else
    return wxSize(GetBitmap().GetWidth(), GetBitmap().GetHeight());
#endif
}

void ScalableButton::sys_color_changed()
{
    Slic3r::GUI::wxGetApp().UpdateDarkUI(this, m_has_border);
    if (m_current_icon_name.empty())
        return;
    wxBitmapBundle bmp = *get_bmp_bundle(m_current_icon_name, m_bmp_width, m_bmp_height);
    SetBitmap(bmp);
    SetBitmapCurrent(bmp);
    SetBitmapPressed(bmp);
    SetBitmapFocus(bmp);
    if (!m_disabled_icon_name.empty())
        SetBitmapDisabled(*get_bmp_bundle(m_disabled_icon_name, m_bmp_width, m_bmp_height));
    if (!GetLabelText().IsEmpty())
        SetBitmapMargins(int(0.5 * em_unit(m_parent)), 0);
}

// ----------------------------------------------------------------------------
// BlinkingBitmap
// ----------------------------------------------------------------------------

BlinkingBitmap::BlinkingBitmap(wxWindow* parent, const std::string& icon_name) :
    wxStaticBitmap(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(int(1.6 * Slic3r::GUI::wxGetApp().em_unit()), -1))
{
    bmp = ScalableBitmap(parent, icon_name);
}

void BlinkingBitmap::invalidate()
{
    this->SetBitmap(wxNullBitmap);
}

void BlinkingBitmap::activate()
{
    this->SetBitmap(bmp.bmp());
    show = true;
}

void BlinkingBitmap::blink()
{
    show = !show;
    this->SetBitmap(show ? bmp.bmp() : wxNullBitmap);
}

namespace Slic3r {
namespace GUI {

void Highlighter::set_timer_owner(wxWindow* owner, int timerid/* = wxID_ANY*/)
{
    m_timer.SetOwner(owner, timerid);
    bind_timer(owner);
}

bool Highlighter::init(bool input_failed)
{
    if (input_failed)
        return false;

    m_timer.Start(300, false);
    return true;
}
void Highlighter::invalidate()
{
    if (m_timer.IsRunning())
        m_timer.Stop();
    m_blink_counter = 0;
}

void Highlighter::blink()
{
    if ((++m_blink_counter) == 11)
        invalidate();
}

// HighlighterForWx

void HighlighterForWx::bind_timer(wxWindow* owner)
{
    owner->Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        blink();
    });
}

// using OG_CustomCtrl where arrow will be rendered and flag indicated "show/hide" state of this arrow
void HighlighterForWx::init(std::pair<OG_CustomCtrl*, bool*> params)
{
    invalidate();
    if (!Highlighter::init(!params.first && !params.second))
        return;

    assert(m_blinking_custom_ctrls.empty());
    m_blinking_custom_ctrls.push_back({params.first, params.second});

    BlinkingCustomCtrl &blinking_custom_ctrl = m_blinking_custom_ctrls.back();
    *blinking_custom_ctrl.show_blink_ptr     = true;
    blinking_custom_ctrl.custom_ctrl_ptr->Refresh();
}

void HighlighterForWx::init(const std::vector<std::pair<OG_CustomCtrl *, bool *>> &blinking_custom_ctrls_params)
{
    this->invalidate();

    const bool input_failed = blinking_custom_ctrls_params.empty() ||
                              std::any_of(blinking_custom_ctrls_params.cbegin(), blinking_custom_ctrls_params.cend(),
                                          [](auto &params) { return params.first == nullptr || params.second == nullptr; });

    if (!Highlighter::init(input_failed))
        return;

    assert(m_blinking_custom_ctrls.empty());
    for (const std::pair<OG_CustomCtrl *, bool *> &blinking_custom_ctrl_params : blinking_custom_ctrls_params) {
        m_blinking_custom_ctrls.push_back({blinking_custom_ctrl_params.first, blinking_custom_ctrl_params.second});

        BlinkingCustomCtrl &blinking_custom_ctrl = m_blinking_custom_ctrls.back();
        *blinking_custom_ctrl.show_blink_ptr     = true;
        blinking_custom_ctrl.custom_ctrl_ptr->Refresh();
    }
}

// - using a BlinkingBitmap. Change state of this bitmap
void HighlighterForWx::init(BlinkingBitmap* blinking_bmp)
{
    invalidate();
    if (!Highlighter::init(!blinking_bmp))
        return;

    m_blinking_bitmap = blinking_bmp;
    m_blinking_bitmap->activate();
}

void HighlighterForWx::invalidate()
{
    Highlighter::invalidate();

    if (!m_blinking_custom_ctrls.empty()) {
        for (BlinkingCustomCtrl &blinking_custom_ctrl : m_blinking_custom_ctrls) {
            assert(blinking_custom_ctrl.is_valid());
            *blinking_custom_ctrl.show_blink_ptr = false;
            blinking_custom_ctrl.custom_ctrl_ptr->Refresh();
        }

        m_blinking_custom_ctrls.clear();
    } else if (m_blinking_bitmap) {
        m_blinking_bitmap->invalidate();
        m_blinking_bitmap = nullptr;
    }
}

void HighlighterForWx::blink()
{
    if (!m_blinking_custom_ctrls.empty()) {
        for (BlinkingCustomCtrl &blinking_custom_ctrl : m_blinking_custom_ctrls) {
            assert(blinking_custom_ctrl.is_valid());
            *blinking_custom_ctrl.show_blink_ptr = !*blinking_custom_ctrl.show_blink_ptr;
            blinking_custom_ctrl.custom_ctrl_ptr->Refresh();
        }
    } else if (m_blinking_bitmap) {
        m_blinking_bitmap->blink();
    } else {
        return;
    }

    Highlighter::blink();
}

}// GUI
}//Slicer




