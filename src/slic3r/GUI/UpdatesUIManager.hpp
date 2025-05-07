///|/ Copyright (c) Prusa Research 2018 - 2020 Oleksandra Iushchenko @YuSanka
///|/
#ifndef slic3r_GUI_UpdatesUIManager_hpp_
#define slic3r_GUI_UpdatesUIManager_hpp_

#include "GUI_Utils.hpp"
#include <boost/filesystem.hpp>
#include <set>
#include <vector>

class wxWindow;
class wxEvent;
class wxSizer;
class wxFlexGridSizer;

namespace Slic3r { 

class PresetUpdaterWrapper;

namespace GUI {
class RepositoryUpdateUIManager
{
    struct OnlineEntry {
        OnlineEntry(bool use, const std::string &id, const std::string &name, const std::string &description, const std::string &visibility, bool not_in_manifest) :
            use(use), id(id), name(name), description(description), visibility(visibility) ,not_in_manifest(not_in_manifest) {}

        bool            use;
        std::string     id;
        std::string     name;
        std::string     description;
        std::string   	visibility;
        bool            not_in_manifest;
    };

    struct OfflineEntry {
        OfflineEntry(bool use, const std::string &id, const std::string &name, const std::string &description, const std::string &source, bool is_ok, boost::filesystem::path source_path) :
            use(use), id(id), name(name), description(description), source(source), is_ok(is_ok), source_path(source_path) {}

        bool            use;
        std::string     id;
        std::string     name;
        std::string     description;
        std::string   	source;
        bool            is_ok;
        boost::filesystem::path source_path;
    };

    PresetUpdaterWrapper*       m_puw           { nullptr };
    wxWindow*                   m_parent        { nullptr };
    wxSizer*                    m_main_sizer    { nullptr };

    wxFlexGridSizer*            m_online_sizer  { nullptr };
    wxFlexGridSizer*            m_offline_sizer { nullptr };

    wxButton*                   m_load_btn      { nullptr };

    std::vector<OnlineEntry>    m_online_entries;
    std::vector<OfflineEntry>   m_offline_entries;

    std::set<std::string>       m_selected_uuids;
    bool                        m_is_selection_changed{false};

    void fill_entries(bool init_selection = false);
    void fill_grids();

    void remove_offline_repos(const std::string& id);
    void load_offline_repos();
    void check_selection();

public:
    RepositoryUpdateUIManager() {}
    RepositoryUpdateUIManager(wxWindow* parent, Slic3r::PresetUpdaterWrapper* puw, int em);
    ~RepositoryUpdateUIManager() {}

    void update();

    wxSizer*    get_sizer() { return m_main_sizer; }
    bool        set_selected_repositories();
    bool        is_selection_changed() const { return m_is_selection_changed; }
    bool        has_selections()       const { return !m_selected_uuids.empty(); }
    const std::set<std::string>& get_selected_uuids() const { return m_selected_uuids; }
};

class ManagePresetRepositoriesDialog : public DPIDialog
{
public:
    ManagePresetRepositoriesDialog(PresetUpdaterWrapper* puw);
    ~ManagePresetRepositoriesDialog() {}

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;
    
private:

    std::unique_ptr<RepositoryUpdateUIManager> m_manager    { nullptr };

    void onCloseDialog(wxEvent &);
    void onOkDialog(wxEvent &);
};

} // namespace GUI
} // namespace Slic3r

#endif
