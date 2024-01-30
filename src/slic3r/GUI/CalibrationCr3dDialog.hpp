#ifndef slic3r_GUI_CalibrationCr3dDialog_hpp_
#define slic3r_GUI_CalibrationCr3dDialog_hpp_

#include "CalibrationAbstractDialog.hpp"

namespace Slic3r { 
namespace GUI {

class CalibrationCr3dAbstractDialog : public CalibrationAbstractDialog
{

public:
    CalibrationCr3dAbstractDialog(GUI_App* app, MainFrame* mainframe, std::string name) : CalibrationAbstractDialog(app, mainframe, name) { }
    virtual ~CalibrationCr3dAbstractDialog(){ }
    
protected:
    void create_geometry(std::string cube_path);
};

class CalibrationCr3dCubeDialog : public CalibrationCr3dAbstractDialog
{

public:
    CalibrationCr3dCubeDialog(GUI_App* app, MainFrame* mainframe) : CalibrationCr3dAbstractDialog(app, mainframe, "Calibration CR-3D Cube") {
        create("/calibration/cr3d/cube", "index.html");
    }

    virtual ~CalibrationCr3dCubeDialog(){ }
    
protected:
    void create_buttons(wxStdDialogButtonSizer* sizer) override;
    void create_geometry_single(wxCommandEvent& event_args) { create_geometry("/cube/CR-3D_Calibration_Cube.3mf"); }
    void create_geometry_dual(wxCommandEvent& event_args) { create_geometry("/cube/CR-3D_Dual_Calibration_Cube_Extruder.3mf"); }
};

class CalibrationCr3dSampleCardDialog : public CalibrationCr3dAbstractDialog
{

public:
    CalibrationCr3dSampleCardDialog(GUI_App* app, MainFrame* mainframe) : CalibrationCr3dAbstractDialog(app, mainframe, "CR-3D Sample Keycard & Tray") {
        create("/calibration/cr3d/samplecard", "index.html");
    }

    virtual ~CalibrationCr3dSampleCardDialog(){ }
    
protected:
    void create_buttons(wxStdDialogButtonSizer* sizer) override;
    void create_geometry_box(wxCommandEvent& event_args) { create_geometry("/samplecard/CR-3D_Filament-Sample_Box_V1.0.3mf"); }
    void create_geometry_card(wxCommandEvent& event_args) { create_geometry("/samplecard/CR-3D_Filament-Sample_Test_V1.2.3mf"); }
};

class CalibrationCr3dMasterSpoolDialog : public CalibrationCr3dAbstractDialog
{

public:
    CalibrationCr3dMasterSpoolDialog(GUI_App* app, MainFrame* mainframe) : CalibrationCr3dAbstractDialog(app, mainframe, "CR-3D Masterspool") {
        create("/calibration/cr3d/masterspool", "index.html");
    }

    virtual ~CalibrationCr3dMasterSpoolDialog(){ }
    
protected:
    void create_buttons(wxStdDialogButtonSizer* sizer) override;
    void create_geometry_base(wxCommandEvent& event_args) { create_geometry("/masterspool/CR-3D_Design_MasterSpool_Grundteil.3mf"); }
    void create_geometry_counterpart(wxCommandEvent& event_args) { create_geometry("/masterspool/CR-3D_Design_MasterSpool_Gegenstück.3mf"); }
};

} // namespace GUI
} // namespace Slic3r

#endif
