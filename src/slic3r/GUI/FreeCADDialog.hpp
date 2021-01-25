#ifndef slic3r_GUI_FreeCADDialog_hpp_
#define slic3r_GUI_FreeCADDialog_hpp_

#include <map>
#include <vector>
#include <regex>

#include "GUI_App.hpp"

#include <wx/combobox.h>
#include <wx/gbsizer.h>
#include <wx/stc/stc.h>

namespace Slic3r { 
namespace GUI {

//can't be defeined here, so it will be defined in cpp (because of include sheanigans)
class ExecVar;

enum PyCommandType : uint16_t {
    pctNONE = 0x0,
    pctOPERATION = 0x1 << 0,
    pctOBJECT = 0x1 << 1,
    pctMODIFIER = 0x1 << 2,
    pctNO_PARAMETER = 0x1 << 3,
    pctDO_NOT_SHOW = 0x1 << 4
};

class PyCommand {
public:
    wxString name;
    PyCommandType type;
    wxString tooltip;
    std::vector<std::string> args;
    PyCommand(wxString lbl, PyCommandType modifier) : name(lbl), type(modifier) { }
    PyCommand(wxString lbl, uint16_t modifier) : name(lbl), type(PyCommandType(modifier)) {}
    PyCommand(wxString lbl, PyCommandType modifier, std::string tooltip) : name(lbl), type(modifier), tooltip(tooltip) {}
    PyCommand(wxString lbl, uint16_t modifier, std::string tooltip) : name(lbl), type(PyCommandType(modifier)), tooltip(tooltip) {}
    PyCommand(wxString lbl, PyCommandType modifier, std::initializer_list<const char*> args, std::string tooltip) : name(lbl), type(modifier), tooltip(tooltip) { for(const char* arg: args) this->args.emplace_back(arg); }
    PyCommand(wxString lbl, uint16_t modifier, std::initializer_list<const char*> args, std::string tooltip) : name(lbl), type(PyCommandType(modifier)), tooltip(tooltip) { for (const char* arg : args) this->args.emplace_back(arg); }
};

class FreeCADDialog : public DPIDialog
{

public:
    FreeCADDialog(GUI_App* app, MainFrame* mainframe);
    virtual ~FreeCADDialog();
    
protected:
    void close_me(wxCommandEvent& event_args);
    void createSTC();

    bool init_start_python();
    void create_geometry(wxCommandEvent& event_args);
    bool end_python();

    void new_script(wxCommandEvent& event_args) { m_text->ClearAll(); }
    void load_script(wxCommandEvent& event_args);
    void save_script(wxCommandEvent& event_args);
    void quick_save(wxCommandEvent& event_args);
    void on_dpi_changed(const wxRect& suggested_rect) override;

    void on_word_change_for_autocomplete(wxStyledTextEvent& event);
    void on_char_add(wxStyledTextEvent& event);
    void on_key_type(wxKeyEvent& event);
    void on_char_type(wxKeyEvent& event);
    void on_autocomp_complete(wxStyledTextEvent& event);
    bool write_text_in_file(const wxString &towrite, const boost::filesystem::path &file);
    bool load_text_from_file(const boost::filesystem::path &file);
    void test_update_script_file(std::string &json);
    void comment(bool is_switch);

    wxStyledTextCtrl* m_text;
    wxTextCtrl* m_errors;
    wxTextCtrl* m_help;
    MainFrame* main_frame;
    GUI_App* gui_app;
    wxGridBagSizer* main_sizer;
    wxComboBox* cmb_add_replace;

    std::vector<PyCommand> commands;

    std::regex word_regex;
    bool update_done = false;

    boost::filesystem::path opened_file;
    ExecVar* exec_var = nullptr;

    bool ready = false;

protected:
    const PyCommand* get_command(const wxString &name) const;
};

} // namespace GUI
} // namespace Slic3r

#endif
