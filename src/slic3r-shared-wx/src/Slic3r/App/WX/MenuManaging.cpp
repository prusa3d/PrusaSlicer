#include "Slic3r/App/WX/MenuManaging.hpp"
#include "Slic3r/App/WX/BitmapGetters.hpp"

#include <set>
#include <map>

namespace Slic3r::App::WX {

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

}


