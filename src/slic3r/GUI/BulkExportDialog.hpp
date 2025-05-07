///|/ Copyright (c) Prusa Research 2020 - 2023 Oleksandra Iushchenko @YuSanka, David Kocík @kocikdav, Lukáš Matěna @lukasmatena
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#pragma once

//#include <wx/gdicmn.h>

#include <boost/filesystem/path.hpp>
#include "wxExtensions.hpp"
#include "GUI_Utils.hpp"
#include <optional>

#include "Widgets/CheckBox.hpp"

class wxString;
class wxStaticText;
class wxTextCtrl;
class wxStaticBitmap;
class wxFlexGridSizer;

namespace Slic3r {
class Print;
struct GCodeProcessorResult;

namespace GUI {

class BulkExportDialog : public DPIDialog
{
public:
    enum class ItemStatus { Valid, NoValid, Warning };

    struct Item
    {
        using Validator = std::function<
            std::pair<BulkExportDialog::ItemStatus, wxString>(
                boost::filesystem::path,
                std::string
            )
        >;
        Item(
            wxWindow *parent,
            wxFlexGridSizer *sizer,
            const std::optional<const boost::filesystem::path>& path,
            const int bed_index,
            Validator validator
        );
        Item(const Item &) = delete;
        Item& operator=(const Item &) = delete;
        Item(Item &&) = delete;
        Item& operator=(Item &&) = delete;

        // Item cannot have copy or move constructors, because a wx event binds
        // directly to its address.

        void update_valid_bmp();
        bool is_valid()   const { return m_status != ItemStatus::NoValid; }
        bool is_warning() const { return m_status == ItemStatus::Warning; }

        boost::filesystem::path path;
        int bed_index{};
        bool selected{true};

    private:
        ItemStatus m_status{ItemStatus::NoValid};
        wxWindow *m_parent{nullptr};
        wxStaticBitmap *m_valid_bmp{nullptr};
        wxTextCtrl *m_text_ctrl{nullptr};
        ::CheckBox *m_checkbox{nullptr};
        Validator m_validator;
        boost::filesystem::path m_directory{};

        void init_input_name_ctrl(wxFlexGridSizer*row_sizer, const std::string &path);
        void init_selection_ctrl(wxFlexGridSizer*row_sizer, int bed_index);
        void update();
    };

private:
    // This must be a unique ptr, because Item does not have copy nor move constructors.
    std::vector<std::unique_ptr<Item>> m_items;
    wxFlexGridSizer*m_sizer{nullptr};
    wxString m_title;
    std::string m_unusable_symbols;    
public:

    BulkExportDialog(const std::vector<std::pair<int, std::optional<boost::filesystem::path>>> &paths, const wxString& title, const std::string& unusable_symbols);
    std::vector<std::pair<int, std::optional<boost::filesystem::path>>> get_paths() const;
    bool has_warnings() const;

protected:
    void on_dpi_changed(const wxRect &) override;
    void on_sys_color_changed() override {}

private:
    void AddItem(const std::optional<const boost::filesystem::path>& path, int bed_index);
    void accept();
    bool enable_ok_btn() const;
};

} // namespace GUI
}
