#pragma once
#include <optional>
#include <string>
#include <vector>

#include "Slic3r/App/Lua/PluginRegistry.hpp"
#include "Slic3r/App/Lua/IPluginRescanListener.hpp"
#include "Slic3r/App/Lua/IPluginInstallationListener.hpp"
#include "Slic3r/App/Lua/MarketplaceClient.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Emboss/IFontManager.hpp"
#include "Slic3r/Biz/Platform//WithListeners.hpp"
#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::App::Lua {

class PluginDialog;

/**
 * One installed plugin bundle, as the marketplace webview needs to see it.
 * `id` is the bundle id from manifest.json, i.e. the marketplace asset id - not
 * the per-.lua plugin id the registry derives from it.
 */
struct InstalledBundle
{
    std::string id;
    std::string version;
};

class PluginSystem : public WithListeners<IPluginRescanListener, IPluginInstallationListener>, IMarketplaceClientListener
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

    /**
     * Every installed bundle with its version, including the ones shipped in
     * resources. The same id can appear twice (shipped plus user-installed);
     * consumers are expected to keep the highest version.
     */
    std::vector<InstalledBundle> installed_bundles() const;

    void install_file(
        const std::string& zip_file_path,
        PublicKeyLookupStrategy key_lookup_strategy = PublicKeyLookupStrategy::Author
    );
    void install_marketplace_bundle(const BundleRequestConfig& config);

    Yoga::Passthrough<PluginDialog>& init_dialog();

    void on_download_error(const std::string& plugin_id, const std::string& message) override;
    bool on_bundle_verified(const VerifiedBundleData& data) override;
    void on_bundle_downloaded(
        const std::string& plugin_id,
        const std::string& version,
        const std::string& zip_path
    ) override;
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
    MarketplaceClient m_marketplace_client;
};

} // namespace Slic3r::App::Lua
