///|/ Copyright (c) Prusa Research 2020 - 2023 Oleksandra Iushchenko @YuSanka, David Kocík @kocikdav, Lukáš Matěna @lukasmatena
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_SavePresetDialog_hpp_
#define slic3r_SavePresetDialog_hpp_

//#include <wx/gdicmn.h>

#include "libslic3r/Preset.hpp"
#include "wxExtensions.hpp"
#include "GUI_Utils.hpp"

class wxString;
class wxStaticText;
class wxComboBox;
class wxTextCtrl;
class wxStaticBitmap;

namespace Slic3r {

namespace GUI {

class SavePresetDialog : public DPIDialog
{
    enum ActionType
    {
        ChangePreset,
        AddPreset,
        Switch, 
        UndefAction
    };
public:
    struct Item
    {
        enum class ValidationType
        {
            Valid,
            NoValid,
            Warning
        };

        // Item as an item inside of the SavePresetDialog
        Item(Preset::Type type, const std::string& suffix, wxBoxSizer* sizer, SavePresetDialog* parent, bool is_for_multiple_save);

        // Item as a separate control(f.e. as a part of ConfigWizard to check name of the new custom priter)
        Item(wxWindow* parent, wxBoxSizer* sizer, const std::string& def_name, PresetBundle* preset_bundle, PrinterTechnology pt = ptFFF);

        void            update_valid_bmp();
        void            accept();
        void            Enable(bool enable = true);

        bool            is_valid()      const { return m_valid_type != ValidationType::NoValid; }
        Preset::Type    type()          const { return m_type; }
        std::string     preset_name()   const;

        struct PresetName {
            std::string casei_name;
            std::string name;

            bool operator<(const PresetName& other) const { return other.casei_name > this->casei_name; }
        };

    private:
        Preset::Type    m_type {Preset::TYPE_INVALID};
        std::string		m_preset_name;
        bool            m_use_text_ctrl {true};

        PrinterTechnology   m_printer_technology {ptAny};
        ValidationType      m_valid_type    {ValidationType::NoValid};
        wxWindow*           m_parent        {nullptr};
        wxStaticBitmap*     m_valid_bmp     {nullptr};
        wxComboBox*         m_combo         {nullptr};
        wxTextCtrl*         m_text_ctrl     {nullptr};
        wxStaticText*       m_valid_label   {nullptr};

        const PresetCollection* m_presets       {nullptr};
        const PresetBundle*     m_preset_bundle {nullptr};

        std::vector<PresetName> m_casei_preset_names;

        std::string get_init_preset_name(const std::string &suffix);
        void        init_input_name_ctrl(wxBoxSizer *input_name_sizer, std::string preset_name);
        void        init_casei_preset_names();
        const Preset*   get_existing_preset() const ;

        void        update();
    };
private:
    std::vector<Item*>   m_items;

    wxBoxSizer*         m_presets_sizer     {nullptr};
    wxStaticText*       m_label             {nullptr};
    wxBoxSizer*         m_radio_sizer       {nullptr};  
    ActionType          m_action            {UndefAction};
    wxCheckBox*         m_template_filament_checkbox {nullptr};

    std::string         m_ph_printer_name;
    std::string         m_old_preset_name;
    bool                m_use_for_rename{false};
    wxString            m_info_line_extention{wxEmptyString};

    PresetBundle*       m_preset_bundle{ nullptr };

public:

    const wxString& get_info_line_extention() { return m_info_line_extention; }

    SavePresetDialog(wxWindow* parent, std::vector<Preset::Type> types, std::string suffix = "", bool template_filament = false, PresetBundle* preset_bundle = nullptr);
    SavePresetDialog(wxWindow* parent, Preset::Type type, const wxString& info_line_extention);
    ~SavePresetDialog() override;

    void AddItem(Preset::Type type, const std::string& suffix, bool is_for_multiple_save);

    const PresetBundle* get_preset_bundle() const { return m_preset_bundle; }
    std::string     get_name();
    std::string     get_name(Preset::Type type);

    bool enable_ok_btn() const;
    void add_info_for_edit_ph_printer(wxBoxSizer *sizer);
    void update_info_for_edit_ph_printer(const std::string &preset_name);
    bool Layout() override;
    bool is_for_rename() { return m_use_for_rename; }

    bool get_template_filament_checkbox();
protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override {}

private:
    void build(std::vector<Preset::Type> types, std::string suffix = "", bool template_filament = false);
    void update_physical_printers(const std::string& preset_name);
    void accept();
};

} // namespace GUI
} // namespace Slic3r

#endif
