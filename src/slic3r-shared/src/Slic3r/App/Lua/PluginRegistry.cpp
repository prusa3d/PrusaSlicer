#include "Slic3r/App/Lua/PluginRegistry.hpp"

#include "Slic3r/Version.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Log.hpp"

#include <ranges>
#include <set>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/directory.hpp>
#include <fmt/ranges.h>

namespace Slic3r::App::Lua {

namespace fs = boost::filesystem;

namespace {

fs::path get_bundle_base_path(const std::string& bundle_id)
{
    return fs::path{data_dir()} / "lua" / bundle_id;
}

}

PluginRegistry::PluginRegistry() :
    m_key_registry(fs::path{data_dir()} / "authorized_authors")
{}

void PluginRegistry::clear()
{
    m_bundles.clear();
    m_plugins.clear();
}

void PluginRegistry::scan(const std::string& path, bool is_protected)
{
    struct PluginPath
    {
        std::string bundle_id;
        Plugin* plugin;
    };

    std::map<std::string, std::vector<PluginPath>> plugin_menu_items;

    fs::path p(path);
    if (!fs::exists(p)) {
        return;
    }

    auto slicer_ver = Semver::parse(VERSION);

    for (const auto& entry : fs::directory_iterator{p}) {
        if (!entry.is_directory()) {
            if (entry.path().extension() == ".lua") {
                std::string file_path = entry.path().string();
                SPDLOG_WARN(
                    "File '{}' will be ignored as it is not located in plugin bundle directory",
                    file_path
                );
            }
            continue;
        }

        auto bundle_path = entry.path().string();
        if (!PluginBundle::is_plugin_bundle_dir(bundle_path)) {
            SPDLOG_WARN("Directory '{}' is not a plugin bundle", bundle_path);
            continue;
        }

        PluginBundle
            bundle{Biz::Crypto::create_directory_source(bundle_path), bundle_path, is_protected};
        auto result = bundle.load_meta();
        if (!result.has_value()) {
            SPDLOG_ERROR("Error loading plugin bundle '{}': {}", bundle_path, result.error());
            continue;;
        }

        if (bundle.meta().min_slicer_version > slicer_ver) {
            SPDLOG_INFO(
                "Plugin Bundle {} requires minimal version of Slicer {}, but current version is {}, skipping",
                bundle_path,
                bundle.meta().min_slicer_version.to_string(),
                slicer_ver->to_string()
            );
            continue;
        }
        if (bundle.meta().max_slicer_version.has_value()
            && bundle.meta().max_slicer_version.value() < slicer_ver)
        {
            SPDLOG_INFO(
                "Plugin Bundle {} requires max version of Slicer {}, but current version is {}, skipping",
                bundle_path,
                bundle.meta().max_slicer_version.value().to_string(),
                slicer_ver->to_string()
            );
            continue;
        }

        if (auto it = std::ranges::find_if(m_bundles, [bundle_id = bundle.meta().id](const auto& pb) { return pb.meta().id == bundle_id; }); it != m_bundles.end())
        {
            const auto inserted_version  = it->meta().version;
            const auto to_insert_version = bundle.meta().version;
            if (to_insert_version <= inserted_version) {
                SPDLOG_INFO(
                    "Skipping loading bundle {} in {}, as its version {} is older than the installed {}",
                    bundle.meta().id,
                    bundle_path,
                    to_insert_version.to_string(),
                    inserted_version.to_string()
                );
                continue;
            }
        }

        try {
            auto plugins = bundle.load_plugins();
            for (auto& plugin : plugins) {
                // make copy of metadata before moving plugin
                const auto plugin_id = plugin.meta().id;
                const auto plugin_path = plugin.path();
                auto [it, inserted] = m_plugins.emplace(plugin_id, std::move(plugin));
                if (!inserted) {
                    SPDLOG_ERROR(
                        "Plugin ID: {} of {} already registered by {}",
                        plugin_id,
                        plugin_path,
                        it->second.path()
                    );
                } else if (it->second.meta().type == PluginType::ProjectPlugin
                       && !it->second.meta().menu.empty()) {
                    auto menu_item = fmt::to_string(fmt::join(it->second.meta().menu, "/"));
                    plugin_menu_items[menu_item].emplace_back(bundle.meta().id, &it->second);
                }
            }

            m_bundles.emplace_back(std::move(bundle));
        } catch (std::exception& e) {
            SPDLOG_ERROR("Loading plugin bundle {} failed with error: {}", bundle_path, e.what());
        }
    }

    for (auto& [menu_item, conflicting_plugins] : plugin_menu_items) {
        if (conflicting_plugins.size() <= 1) {
            continue;
        }

        SPDLOG_WARN(
            "Found conflicting menu item: {} registered in plugins: {}",
            menu_item,
            fmt::join(
                conflicting_plugins
                    | std::views::
                        transform([](const auto& p) { return p.plugin->path(); }),
                ", "
            )
        );

        std::set<std::string> bundle_ids;
        std::ranges::copy(
            conflicting_plugins
                | std::views::transform([](const auto& p) { return p.bundle_id; }),
            std::inserter(bundle_ids, bundle_ids.begin())
        );

        const bool use_bundle_id_suffix = bundle_ids.size() == conflicting_plugins.size();

        for (size_t i = 0; i < conflicting_plugins.size(); i++) {
            auto& plugin_path = conflicting_plugins.at(i);
            auto suffix       = use_bundle_id_suffix ?
                      fmt::format(" ({})", plugin_path.bundle_id) :
                      fmt::format(" ({})", i + 1);
            plugin_path.plugin->meta().menu.back() += suffix;
        }
    }
}

PluginInstallResult PluginRegistry::install(PluginBundle& bundle, PublicKeyLookupStrategy lookup_strategy)
{
    std::string key_name;
    try {
        switch (lookup_strategy) {
        case PublicKeyLookupStrategy::Author:
            key_name = bundle.meta().author;
            break;

        case PublicKeyLookupStrategy::VersionedBundle:
            key_name = fmt::format("{}@{}", bundle.meta().id, bundle.meta().version.to_string());
            break;
        }

        ASSERT(!key_name.empty());

        auto author_key = m_key_registry.load_key(key_name);
        if (!bundle.verify(author_key)) {
            return tl::unexpected(
                Biz::_u8L("Plugin installation failed: Integrity verification failed for bundle ")
                + bundle.meta().id
            );
        }
    } catch (Biz::Crypto::CryptoException&) {
        return tl::unexpected(
            fmt::format(
                fmt::runtime(
                    // TRN First {} is a generic key name (followed by .pem in text as the file extension, second {} is a plugin bundle identifier
                    Biz::_u8L("Plugin installation failed: Cannot load public key {}.pem bundle {}")
                ),
                key_name,
                bundle.meta().id
            )
        );
    }

    if (!is_safe_path_component(bundle.meta().id)) {
        return tl::unexpected(
            Biz::_u8L("Plugin installation failed: Invalid bundle id ") + bundle.meta().id
        );
    }
    auto dest_dir = get_bundle_base_path(bundle.meta().id);
    if (fs::exists(dest_dir)) {
        fs::remove_all(dest_dir);
    }
    if (auto result = bundle.install(dest_dir.string()); !result.has_value()) {
        return tl::unexpected{Biz::_u8L("Plugin installation failed: ") + result.error()};
    }

    return {};
}

PluginInstallResult
PluginRegistry::uninstall(const std::string& bundle_id, const std::string& version)
{
    Semver ver;
    if (auto ver_result = Semver::parse(version); !ver_result.has_value()) {
        return tl::unexpected{fmt::format(
            // TRN The {} is a string with version failed to parse
            fmt::runtime(Biz::_u8L("Plugin uninstall failed: Cannot parse version '{}'")),
            version
        )};
    } else {
        ver = ver_result.value();
    }

    auto bundle_it = std::ranges::find_if(
        m_bundles,
        [&](const auto& b) { return b.meta().id == bundle_id && b.meta().version == ver; }
    );
    if (bundle_it == m_bundles.end()) {
        return tl::unexpected{fmt::format(
            // TRN First {} is a plugin bundle identifier, the second {} is version
            fmt::runtime(Biz::_u8L("Plugin uninstall failed: Cannot find bundle '{}@{}'")),
            bundle_id,
            version
        )};
    }

    auto base_dir = get_bundle_base_path(bundle_it->meta().id);
    return bundle_it->uninstall(base_dir.string());
}

} // namespace Slic3r::App::Lua
