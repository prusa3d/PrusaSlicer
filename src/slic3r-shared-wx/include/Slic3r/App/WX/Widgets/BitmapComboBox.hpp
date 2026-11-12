#pragma once

#include "WXComboBox.hpp"

// ---------------------------------
// ***  BitmapComboBox  ***
// ---------------------------------
namespace Slic3r::App::WX::Widgets {

// BitmapComboBox used to presets list on Sidebar and Tabs
//class BitmapComboBox : public wxBitmapComboBox
class BitmapComboBox : public WXComboBox
{
public:
BitmapComboBox( wxWindow*       parent,
                wxWindowID      id      = wxID_ANY,
                const wxString& value   = wxEmptyString,
                const wxPoint&  pos     = wxDefaultPosition,
                const wxSize&   size    = wxDefaultSize,
                int             n       = 0,
                const wxString  choices[] = NULL,
                long            tyle    = 0);
};

}
