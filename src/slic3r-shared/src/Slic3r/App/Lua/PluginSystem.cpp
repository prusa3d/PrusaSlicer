#include "Slic3r/App/Lua/PluginSystem.hpp"
#include "Slic3r/App/Lua/PluginDialog.hpp"
#include "Slic3r/App/Lua/ProjectApi.hpp"
#include "Slic3r/Biz/Lua/LuaException.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <boost/nowide/config.hpp>
#include <boost/nowide/fstream.hpp>
#include <utility>
#include <imgui/imgui.h>

namespace Slic3r::App::Lua {

namespace {
class PackageRegistry
{
public:
    void register_api(Biz::Lua::LuaEngine& lua)
    {
        m_path_resolver = [&lua](const std::string& file_path) -> std::string
        { return lua.resolve_file(file_path); };
        lua.state()["require"] = [this](sol::this_state ts, std::string module_name) -> sol::object
        { return safe_require(ts, module_name); };
    }
private:
    sol::object safe_require(sol::this_state ts, const std::string& module_name)
    {
        sol::state_view lua(ts);

        // Check the secure cache in the Lua Registry
        // The registry is inaccessible from regular Lua scripts.
        sol::table registry = lua.registry();
        sol::table secure_cache = registry["_SECURE_REQUIRE_CACHE"].get_or_create<sol::table>();

        if (secure_cache[module_name].valid()) {
            // Return cached module
            return secure_cache[module_name];
        }

        // Resolve file
        std::string file_path = m_path_resolver(module_name + ".lua");
        if (file_path.empty()) {
            luaL_error(lua.lua_state(), "System Error: Module '%s' not found.", module_name.c_str());
            return sol::lua_nil;
        }

        // Read the file purely in C++
        boost::nowide::ifstream file(file_path);
        if (!file.is_open()) {
            luaL_error(lua.lua_state(), "System Error: Authorized module '%s' not found on disk.", module_name.c_str());
            return sol::lua_nil;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();

        // Compile the Lua code,
        // prepend @ to file_path so lua error reporting will treat this as file
        sol::load_result loaded_chunk = lua.load(buffer.str(), "@" + file_path);
        if (!loaded_chunk.valid()) {
            sol::error err = loaded_chunk;
            luaL_error(lua.lua_state(), "Syntax Error in module '%s': %s", module_name.c_str(), err.what());
            return sol::lua_nil;
        }

        // Execute the chunk
        sol::protected_function_result result = loaded_chunk();
        if (!result.valid()) {
            sol::error err = result;
            luaL_error(lua.lua_state(), "Runtime Error in module '%s': %s", module_name.c_str(), err.what());
            return sol::lua_nil;
        }

        // Cache the result (If a module returns nothing, Lua requires it to cache as 'true')
        sol::object final_result = result;
        if (final_result.get_type() == sol::type::lua_nil) {
            final_result = sol::make_object(lua, true);
        }

        secure_cache[module_name] = final_result;

        return final_result;
    }
private:
    Biz::Lua::LuaEngine::FilePathResolveFn m_path_resolver;
};
}

PluginSystem::PluginSystem(
    std::initializer_list<PluginPath> plugin_paths,
    Biz::ProjectInteractor& project_interactor,
    Biz::Emboss::IFontManager& font_manager
) :
    m_plugin_paths(plugin_paths),
    m_project_interactor(project_interactor),
    m_font_manager(font_manager),
    m_fav_fonts(font_manager.create_favorit())
{
    m_marketplace_client.add_listener<IMarketplaceClientListener>(this);
}

void PluginSystem::
    install_file(const std::string& zip_file_path, PublicKeyLookupStrategy key_lookup_strategy)
{
    auto zip_source = Biz::Crypto::create_zip_source(zip_file_path);
    if (zip_source == nullptr) {
        std::string error_message = fmt::format(
            fmt::runtime(
                // TRN {} Is a path to file that can't be opened
                Biz::_u8L("Cannot open file: {}")
            ),
            zip_file_path
        );
        invoke_listeners<IPluginInstallationListener>(
            [&error_message](auto* listener)
            { listener->on_plugin_installation_error(error_message); }
        );
        return;

    }
    PluginBundle plugin_bundle{std::move(zip_source), zip_file_path, true};
    if (auto load_meta_result = plugin_bundle.load_meta(); !load_meta_result.has_value()) {
        invoke_listeners<IPluginInstallationListener>(
            [&load_meta_result](auto* listener)
            { listener->on_plugin_installation_error(load_meta_result.error()); }
        );
        return;
    }

    if (auto install_result = m_registry.install(plugin_bundle, key_lookup_strategy);
        !install_result.has_value())
    {
        invoke_listeners<IPluginInstallationListener>(
            [&install_result](auto* listener)
            { listener->on_plugin_installation_error(install_result.error()); }
        );
        return;
    }

    rescan();

    invoke_listeners<IPluginInstallationListener>(
        [&plugin_bundle](auto* listener)
        { listener->on_plugin_installation_succeeded(plugin_bundle.meta()); }
    );

}

void PluginSystem::
    download_file(const std::string& plugin_id, const std::string& version, const std::string& url)
{
    Biz::FileDownloader::FileDownloaderMultiTicket ticket;
    Biz::FileDownloader::FileDownloaderJobInput job_input;
    job_input.file_name = fmt::format("{}@{}.zip", plugin_id, version);
    job_input.file_url = url;
    ticket.jobs.emplace_back(std::move(job_input));

    auto tag = std::make_shared<Biz::FileDownloader::PluginFileDownloaderTag>();
    tag->plugin_id = plugin_id;
    tag->version = version;

    ticket.tag = tag;
    m_project_interactor.file_downloader_interactor().init_multi_job(std::move(ticket));
}

std::vector<InstalledBundle> PluginSystem::installed_bundles() const
{
    const auto& bundles = m_registry.plugin_bundles();
    std::vector<InstalledBundle> result;
    result.reserve(bundles.size());
    for (const auto& bundle : bundles) {
        const auto& meta = bundle.meta();
        if (meta.id.empty()) {
            continue;
        }
        result.push_back({meta.id, meta.version.to_string(), bundle.is_protected()});
    }
    return result;
}

void PluginSystem::install_marketplace_bundle(const BundleRequestConfig& config)
{
    m_marketplace_client.download_bundle(config);
}

void PluginSystem::uninstall_bundle(const std::string& bundle_id, const std::string& version)
{
    if (auto result = m_registry.uninstall(bundle_id, version); result.has_value()) {
        rescan();
        invoke_listeners<IPluginInstallationListener>(
            [&bundle_id, &version](auto* listener)
            { listener->on_plugin_uninstallation_succeeded(bundle_id, version); }
        );
    } else {
        invoke_listeners<IPluginInstallationListener>(
            [&result](auto* listener)
            { listener->on_plugin_uninstallation_error(result.error()); }
        );
    }
}

Yoga::Passthrough<PluginDialog>& PluginSystem::init_dialog()
{
    m_dialog = std::make_unique<PluginDialog>([this](const auto& meta, const auto& params)
    {
        m_current_plugin_data = std::make_optional<PluginData>(meta, params);
        finalize_run();
    }, m_font_manager.get_fonts(), m_fav_fonts.front().path);
    return m_dialog;
}

void PluginSystem::on_download_error(const std::string& plugin_id, const std::string& message)
{
    SPDLOG_ERROR("Marketplace bundle download failed for {}: {}", plugin_id, message);
    std::string error_message = fmt::format(
        fmt::runtime(
            // TRN {0} is a plugin id, {1} describes what went wrong
            Biz::_u8L("Cannot download plugin {0}: {1}")
        ),
        plugin_id,
        message
    );
    invoke_listeners<IPluginInstallationListener>(
        [&error_message](auto* listener)
        { listener->on_plugin_installation_error(error_message); }
    );
}

bool PluginSystem::on_bundle_verified(const VerifiedBundleData& data)
{
    std::string key_id = data.plugin_id;
    try {
        auto version = Semver(data.version);
        key_id = fmt::format("{}@{}", data.plugin_id, version.to_string());
        m_registry.public_key_registry().store_key(key_id, data.public_key);
    } catch (std::exception& e) {
        // Runs on the main thread from the marketplace job callback - letting this
        // escape would take the app down instead of failing the installation.
        SPDLOG_ERROR("Cannot store author public key {}: {}", key_id, e.what());
        on_download_error(data.plugin_id, e.what());
        return false;
    }
    return true;
}

void PluginSystem::on_bundle_downloaded(
    const std::string& plugin_id,
    const std::string& version,
    const std::string& zip_path
)
{
    install_file(zip_path, PublicKeyLookupStrategy::VersionedBundle);
}

void PluginSystem::execute_plugin(const std::string& id)
{
    auto it = m_registry.plugins().find(id);
    if (it == m_registry.plugins().end()) {
        SPDLOG_ERROR("Cannot execute missing plugin id: {}", id);
        return;
    }
    const auto& plugin = it->second;
    ASSERT(m_dialog.get() != nullptr);
    try {
        m_dialog->show_plugin(
            plugin.meta(),
            m_last_plugin_data.has_value() && m_last_plugin_data->meta.id == id ?
                m_last_plugin_data->param_values :
                PluginParamValueMap{}
        );
        Biz::Platform::PlatformServices::instance().render_request_handler().request_render();
    } catch (std::exception& e) {
        SPDLOG_ERROR("Executin plugin {} failed with error: {}", plugin.path(), e.what());
    }
}

void PluginSystem::finalize_run()
{
    ASSERT(m_current_plugin_data.has_value());

    ProjectApi project_api(m_project_interactor, m_font_manager, m_fav_fonts);
    Biz::Lua::LuaEngine lua;
    lua.open_registry([&project_api](auto& lua) { project_api.register_api(lua); });
    PackageRegistry package_registry;
    lua.open_registry([&package_registry](auto& lua) { package_registry.register_api(lua); });

    const auto& plugin = m_registry.plugins().at(m_current_plugin_data->meta.id);

    try {
        plugin.execute(lua, m_current_plugin_data->param_values);
    } catch (Biz::Lua::LuaException& e) {
        SPDLOG_ERROR(
            "Running plugin {} failed in script {}\n{}",
            plugin.meta().id,
            e.script_path(),
            e.what()
        );
    } catch (std::exception& e) {
        SPDLOG_ERROR("Running plugin {} failed\n{}", plugin.meta().id, e.what());
    }

    m_last_plugin_data = m_current_plugin_data;
    m_current_plugin_data = std::nullopt;

    m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::ExecutePlugin);
}

void PluginSystem::clear()
{
    m_registry.clear();
    m_last_plugin_data = std::nullopt;
    m_current_plugin_data = std::nullopt;
}

void PluginSystem::scan(const std::string& path, bool is_protected)
{
    m_registry.scan(path, is_protected);
}

void PluginSystem::rescan()
{
    clear();
    for (const auto& path : m_plugin_paths) {
        scan(path.path, path.is_protected);
    }
    invoke_listeners<IPluginRescanListener>([this](auto* l) { l->on_plugins_scanned(m_registry); });
}

} // namespace Slic3r::App::Lua
