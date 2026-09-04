#pragma once

#include <wx/dialog.h>

namespace Slic3r::App::WX {

class LongStringsDiffDialog : public wxDialog
{
public:
    LongStringsDiffDialog(
        const wxString& name,
        const wxString& old_value,
        const wxString& mod_value,
        const wxString& new_value,
        const wxString& old_value_header,
        const wxString& mod_value_header,
        const wxString& new_value_header
    );
    ~LongStringsDiffDialog() override = default;
};

}
