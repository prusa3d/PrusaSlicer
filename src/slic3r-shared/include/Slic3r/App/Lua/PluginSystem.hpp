#pragma once
#include <optional>
#include <string>
#include <vector>

#include "Slic3r/App/Lua/PluginRegistry.hpp"
#include "Slic3r/App/Lua/IPluginRescanListener.hpp"
#include "Slic3r/App/Lua/IPluginInstallationListener.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Emboss/IFontManager.hpp"
#include "Slic3r/Biz/Platform//WithListeners.hpp"
#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::App::Lua {

class PluginDialog;

class PluginSystem : public WithListeners<IPluginRescanListener, IPluginInstallationListener>
{
public:
    explicit PluginSystem(
        std::initializer_list<std::string> plugin_paths,
        Biz::ProjectInteractor& project_interactor,
        Biz::Emboss::IFontManager& font_manager
    );
    void execute_plugin(const std::string& id);
    void rescan();
    const auto& plugins() const { return m_registry.plugins(); }

    void install(const std::string& zip_file_path);

    Yoga::Passthrough<PluginDialog>& init_dialog();
private:
    void clear();
    void scan(const std::string& path);
    void finalize_run();
private:
    struct PluginData
    {
        PluginMeta meta;
        PluginParamValueMap param_values;
    };

    PluginRegistry m_registry;
    std::vector<std::string> m_plugin_paths;
    Biz::ProjectInteractor& m_project_interactor;
    Biz::Emboss::IFontManager& m_font_manager;
    std::optional<PluginData> m_current_plugin_data, m_last_plugin_data;
    Yoga::Passthrough<PluginDialog> m_dialog;
};

} // namespace Slic3r::App::Lua
