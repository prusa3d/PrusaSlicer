#ifndef slic3r_GUI_CalibrationAbstractDialog_hpp_
#define slic3r_GUI_CalibrationAbstractDialog_hpp_

#include <wx/wx.h>
#include <map>
#include <vector>

#include "Jobs/ProgressIndicator.hpp"
#include "GUI_App.hpp"
#include "GUI_Utils.hpp"
#include "MainFrame.hpp"
#include "wxExtensions.hpp"
#include <wx/html/htmlwin.h>

namespace Slic3r { 
namespace GUI {

class CalibrationAbstractDialog : public DPIDialog
{

public:
    CalibrationAbstractDialog(GUI_App* app, MainFrame* mainframe, std::string name);
    virtual ~CalibrationAbstractDialog(){ if(gui_app!=nullptr) gui_app->change_calibration_dialog(this, nullptr);}
    
private:
    wxPanel* create_header(wxWindow* parent, const wxFont& bold_font);
protected:
    void create(boost::filesystem::path html_path, std::string html_name, wxSize dialogsize = wxSize(850, 550));
    virtual void create_buttons(wxStdDialogButtonSizer*) = 0;
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void close_me(wxCommandEvent& event_args);
    ModelObject* add_part(ModelObject* model_object, std::string input_file, Vec3d move, Vec3d scale = Vec3d{ 1,1,1 });

    wxHtmlWindow* html_viewer;
    MainFrame* main_frame;
    GUI_App* gui_app;

};


class HtmlDialog : public CalibrationAbstractDialog
{

public:
    HtmlDialog(GUI_App* app, MainFrame* mainframe, std::string title, std::string html_path, std::string html_name) : CalibrationAbstractDialog(app, mainframe, title) { create(html_path, html_name); }
    virtual ~HtmlDialog() {}
protected:
    void create_buttons(wxStdDialogButtonSizer* sizer) override {}

};

class ProgressIndicatorStub : public ProgressIndicator {
public:

    virtual ~ProgressIndicatorStub() override = default;

    virtual void set_range(int range) override {}
    virtual void set_cancel_callback(CancelFn = CancelFn()) override {}
    virtual void set_progress(int pr) override {}
    virtual void set_status_text(const char*) override {}
    virtual int  get_range() const override { return 0; }
};

} // namespace GUI
} // namespace Slic3r

#endif
