#include "DiffNamespace.hpp"

#include "Slic3r/App/WX/WidgetsConfig.hpp"
#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/App/WX/BitmapGetters.hpp"
#include "Slic3r/App/WX/I18N.hpp"

#include "Slic3r/Biz/Config/ConfigSerialize.hpp"
#include "Slic3r/Biz/Config/BedShape.hpp"

#include <Slic3r/Assert.hpp>
#include <wx/checkbox.h>
#include <wx/button.h>

namespace Slic3r::App::WX::Diff {

static std::string get_as_string(const Domain::ConfigItem& item)
{
    if (item.name() == "bed_shape") {
        Biz::Config::BedShape bed_shape(item.get<std::vector<Domain::Vec2d>>());
        return bed_shape.get_full_name_with_params();
    }
    std::string val;
    if (auto var = Slic3r::Biz::value_as_string(item); std::holds_alternative<std::string>(var)) {
        return std::get<std::string>(var);
    } else {
        auto values = std::get<std::vector<std::string>>(var);
        for (size_t id = 0; id < values.size(); id++) {
            val += values[id];
            if (id < values.size() - 1) {
                val += "; ";
            }
        }
    }
    return val;
}

std::string get_display_value_or_na(const Domain::ConfigBox* config, const std::string& key)
{
    if (config) {
        if (const auto override = config->overrides.find(key)) {
            if (config->overrides.get(key)) {
                return get_as_string(*override);
            }
        }
        if (const auto item = config->items.find(key))
            return get_as_string(*item);
    }
    return Biz::_u8L("N/A");
}

BCB::BCB(
    wxWindow* parent,
    std::function<void(int selection, Location location)> fn,
    Location location
) :
    wxComboBox(
        parent,
        wxID_ANY,
        wxEmptyString,
        wxDefaultPosition,
        wxSize(30 * w_config()->em_unit(), 0),
        0,
        NULL,
        wxCB_READONLY
    ),
    m_select_fn(fn),
    m_location(location)
{
    this->Bind(
        wxEVT_COMBOBOX,
        [this](wxCommandEvent& evt)
        {
            int selection = evt.GetSelection();
            if (m_select_fn) {
                m_select_fn(selection, m_location);
            }
        }
    );

    this->Bind(
        wxEVT_SIZE,
        [this](wxSizeEvent& evt)
        {
            evt.Skip();
            this->Refresh();
        }
    );
}

BCB::~BCB()
{
    Clear();
}

void BCB::SetSelection(int n)
{
    if (this->HasClientObjectData()) {
        BCBClientData* obj = dynamic_cast<BCBClientData*>(this->GetClientObject(n));
        if (obj && obj->is_marker()) {
            ASSERT(m_old_selection >= 0);
            this->SetSelection(m_old_selection);
            return;
        }
    }

    wxComboBox::SetSelection(n);
    m_old_selection = n;
}

Row::Row(
    wxWindow* parent_win,
    std::function<void(int selection, Location location)> fn,
    Row* depends_on_row
) :
    wxBoxSizer(wxHORIZONTAL),
    m_select_fn(fn),
    m_depends_on_row(depends_on_row)
{
    const int em = w_config()->em_unit();

    m_checkbox = new wxCheckBox(parent_win, wxID_ANY, wxEmptyString);
    m_checkbox->SetValue(true);
    m_checkbox->SetToolTip(
        _L("When enabled, the difference for this preset type is displayed in the tree below.")
    );

    m_left  = new BCB(parent_win, fn, Location::Left);
    m_right = new BCB(parent_win, fn, Location::Right);

    m_equal_bmp = new wxButton(
        parent_win,
        wxID_ANY,
        wxEmptyString,
        wxDefaultPosition,
        wxDefaultSize,
        wxBU_EXACTFIT
    );
    m_equal_bmp->SetBitmap(*get_bmp_bundle("equal"));
    m_equal_bmp->Bind(
        wxEVT_BUTTON,
        [this](wxEvent&)
        {
            if (state != State::NotEqual) {
                // there is nothing to equalize
                return;
            }

            // We can equalize right to left, only when m_depends_on_row.state == State::Equal
            // OR this row doesn't have m_depends_on_row
            // OR m_depends_on_row is hidden
            if (!m_depends_on_row
                || !m_depends_on_row->IsShown(1)
                || m_depends_on_row->state == State::Equal)
            {
                int left_selection = m_left->GetSelection();
                m_right->SetSelection(left_selection);
                if (m_select_fn) {
                    m_select_fn(left_selection, Location::Right);
                }
            }
        }
    );

    this->Add(m_checkbox, 0, wxALIGN_CENTER_VERTICAL);
    this->Add(m_left, 1, wxEXPAND | wxLEFT | wxRIGHT, em);
    this->Add(m_equal_bmp, 0, wxALIGN_CENTER_VERTICAL);
    this->Add(m_right, 1, wxEXPAND | wxLEFT, em);
}

BCB* Row::operator[](Location location)
{
    return location == Location::Left ? m_left : m_right;
}

BCBClientData* Row::client_data(int selection, Location location)
{
    BCB* cb = (*this)[location];
    if (cb->HasClientObjectData()) {
        return dynamic_cast<BCBClientData*>(cb->GetClientObject(selection));
    }
    return nullptr;
}

BCBClientData* Row::client_data_selected(Location location)
{
    return client_data(selection(location), location);
}

int Row::selection(Location location)
{
    return (*this)[location]->GetSelection();
}

void Row::select(int selection, Location location)
{
    (*this)[location]->SetSelection(selection);
}

void Row::set_state(State new_state)
{
    if (new_state != State::Undef) {
        // check comboboxes for the content
        if (m_left->IsListEmpty() || m_right->IsListEmpty()) {
            // if some of them is empty => it's Undefind state
            new_state = State::Undef;
        }
    }

    state = new_state;

    m_equal_bmp->SetBitmap(*get_bmp_bundle(
        state == State::Equal        ? "equal" :
            state == State::NotEqual ? "not_equal" :
                                       "empty"
    ));

    wxString tooltip = wxEmptyString;
    if (state == State::NotEqual) {
        tooltip = _L("Presets are different.");
        // We can equalize right to left, only when m_depends_on_row.state == State::Equal or this row doesn't have m_depends_on_row;
        if (m_depends_on_row == nullptr || m_depends_on_row->state == State::Equal) {
            tooltip += from_u8("\n")
                + _L("Click this button to select the same preset for the right and left preset.");
        } else {
            tooltip += from_u8("\n")
                + _L("To make these presets identical, presets above them must also match.");
        }
    }
    m_equal_bmp->SetToolTip(tooltip);

    m_checkbox->Enable(new_state != State::Undef);
    m_left->Enable(new_state != State::Undef);
    m_right->Enable(new_state != State::Undef);
}

void Row::set_checkbox_callback(std::function<void()> fn)
{
    m_checkbox->Bind(wxEVT_CHECKBOX, [fn](wxCommandEvent&) { fn(); });
}

bool Row::is_checked_checkbox() const
{
    return m_checkbox->GetValue();
}

void Row::show_checkbox(bool show)
{
    m_checkbox->Show(show);
}

} // namespace Slic3r::App::WX::Diff
