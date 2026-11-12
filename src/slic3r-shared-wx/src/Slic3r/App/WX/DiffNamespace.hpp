#pragma once

#include <wx/sizer.h>
#include <wx/combobox.h>

class wxCheckBox;
class wxButton;

namespace Slic3r::Domain {
struct ConfigBox;
} // namespace Slic3r::Domain

namespace Slic3r::App::WX {

class ScalableButton;

// This namespace contains classes used by DiffDialog

namespace Diff {

std::string get_display_value_or_na(const Domain::ConfigBox* config, const std::string& key);

enum class Location
{
    Left,
    Right
};

enum class State
{
    Undef,
    Equal,
    NotEqual,
};

class BCB : public wxComboBox
{
public:
    BCB(wxWindow* parent, std::function<void(int selection, Location location)> fn, Location location);
    ~BCB();

    void SetSelection(int n) override;

private:
    std::function<void(int selection, Location location)> m_select_fn{nullptr};
    int m_old_selection{-1};
    Location m_location;
};

class BCBClientData : public wxClientData
{
public:
    size_t tool_id{size_t(-1)};
    size_t tool_print_id{size_t(-1)};

    BCBClientData() {}

    BCBClientData(size_t tool_id, size_t tool_print_id) :
        tool_id(tool_id),
        tool_print_id(tool_print_id)
    {}

    bool is_marker()
    {
        return tool_id == tool_print_id && tool_id == size_t(-1);
    }
};

// Box sizer which contains left and right preset lists and makrer button in between them to show a comparison state
struct Row : public wxBoxSizer
{
    State state{State::Undef};

    Row(wxWindow* parent_win, std::function<void(int selection, Location location)> fn, Row* depends_on_row = nullptr);

    BCB* operator[](Location location);
    BCBClientData* client_data(int selection, Location location);
    BCBClientData* client_data_selected(Location location);

    int selection(Location location);
    void select(int selection, Location location);

    void set_state(State state);

    void set_checkbox_callback(std::function<void()> fn);
    bool is_checked_checkbox() const;
    void show_checkbox(bool show);

private:
    wxCheckBox* m_checkbox{nullptr};
    BCB* m_left{nullptr};
    wxButton* m_equal_bmp{nullptr};
    BCB* m_right{nullptr};

    std::function<void(int selection, Location location)> m_select_fn{nullptr};

    // Row on its statate current row state depends
    Row* m_depends_on_row{nullptr};
};

} // namespace Diff
} // namespace Slic3r::App::WX
