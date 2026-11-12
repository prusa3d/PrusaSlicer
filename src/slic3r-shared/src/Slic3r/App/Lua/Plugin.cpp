#include "Slic3r/App/Lua/Plugin.hpp"

#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/Lua/LuaException.hpp"
#include "Slic3r/Biz/Expr/Parser.hpp"
#include "Slic3r/Log.hpp"

#include <fmt/format.h>
#include <algorithm>
#include <cctype>
#include <ranges>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>

namespace Slic3r::App::Lua {

namespace fs = boost::filesystem;

namespace {
const std::unordered_map<PluginType, std::string> PLUGIN_TYPE_NAMES = {
    {PluginType::ProjectPlugin, "project.plugin"},
    {PluginType::PrintMode, "print.print_mode"}
};
}

tl::expected<PluginType, std::string> parse_plugin_type(std::string_view s)
{
    auto r = PLUGIN_TYPE_NAMES | std::views::values;
    const auto it = std::ranges::find(r, s);
    if (it == r.end()) {
        return tl::unexpected{fmt::format("Unknown plugin type: {}", s)};
    }
    return it.base()->first;
}

bool is_path_in_sandbox(
    const boost::filesystem::path& sandbox_path,
    const boost::filesystem::path& tested_path
)
{
    boost::system::error_code ec;

    // 1. Canonicalize the root
    fs::path canonical_root = fs::weakly_canonical(sandbox_path, ec);
    if (ec) {
        return false; // Root directory must exist and be accessible
    }

    // 2. Canonicalize the user-provided path
    // Use weakly_canonical to support paths to files that don't exist yet
    fs::path canonical_user = fs::weakly_canonical(tested_path, ec);
    if (ec) {
        return false;
    }

    // 3. Calculate relative path lexically
    // Note: lexically_relative does not touch the disk, so it doesn't take error_code
    fs::path relative = canonical_user.lexically_relative(canonical_root);

    // 4. Validate the result
    // An empty path or one starting with ".." indicates an escape
    if (relative.empty() || *relative.begin() == "..") {
        return false;
    }

    return true;
}

bool is_safe_path_component(std::string_view token)
{
    constexpr std::size_t max_len = 128;
    if (token.empty() || token.size() > max_len) {
        return false;
    }
    const auto is_alnum = [](unsigned char c) { return std::isalnum(c) != 0; };
    // Windows strips a trailing dot, so "foo." and "foo" would share one folder.
    if (!is_alnum(token.front()) || token.back() == '.') {
        return false;
    }
    return std::ranges::all_of(token, [&](unsigned char c) {
        return is_alnum(c) || c == '.' || c == '_' || c == '-' || c == '+';
    });
}

struct SafeFileResolver
{
    fs::path plugin_path;
    std::string operator()(const std::string& path) const
    {
        auto p = plugin_path.parent_path() / path;
        if (!is_secure_path(p)) {
            throw Biz::Lua::LuaException{
                fmt::format(
                    "Plugin '{}' uses insecure path {}",
                    plugin_path.string(),
                    path.c_str()
                ),
                plugin_path.string()
            };
        }
        return p.string();
    }

private:
    bool is_secure_path(const fs::path& user_path) const {
        const fs::path root = plugin_path.parent_path();
        return is_path_in_sandbox(root, user_path);
    }
};

Plugin::ParseResult
Plugin::parse(Biz::Lua::LuaEngine& lua, const std::string& bundle_id, const std::string& path)
{
    auto& state = lua.state();
    if (!state["info"].is<sol::table>()) {
        return tl::unexpected{"Missing info table"};
    }

    sol::table info = state["info"];
    PluginMeta meta;
    meta.id = bundle_id + "." + info["id"].get<std::string>();
    auto type_result = parse_plugin_type(info["type"].get<std::string>());
    if (!type_result.has_value()) {
        return tl::unexpected{type_result.error()};
    }
    meta.type = type_result.value();
    meta.title = info.get<std::optional<std::string>>("title");

    if (info["menu"].valid()) {
        std::vector<std::string> menu_items;
        std::string menu = info["menu"];
        for (const auto menu_item : std::views::split(menu, '/')) {
            menu_items.emplace_back(menu_item.begin(), menu_item.end());
        }
        meta.menu = std::move(menu_items);
    }

    if (info["params"].valid()) {
        sol::table args = info["params"];
        args.for_each([&meta](const sol::object&, const sol::table& p)
        {
            auto name = p.get<std::string>("name");
            auto label = p.get_or<std::string>("label", name);
            auto type = p.get<std::string>("type");
            std::optional<PluginParamValue> value;
            if (p["default"].valid()) {
                value = p.get<PluginParamValue>("default");
            }
            std::vector<std::string> allowed_values;
            if (auto in_vals = p["allowed_values"]; in_vals.valid()) {
                allowed_values = in_vals.get<std::vector<std::string>>();
            }
            meta.params.emplace_back(name, label, type, value, allowed_values);
        });
    }

    if (info["condition"].valid()) {
        if (!info["condition"].is<std::string>()) {
            return tl::unexpected{"The condition must be a string"};
        }
        try {
            meta.condition = Biz::Expr::Parser().parse(info["condition"].get<std::string>());
        } catch (const Biz::Expr::ParseError& e) {
            return tl::unexpected{fmt::format("Invalid condition: {}", e.what())};
        }
    }

    if (!state["execute"].is<sol::function>()) {
        return tl::unexpected{"Missing execute() function"};
    }

    return Plugin{path, bundle_id, meta};
}

Plugin::Plugin(std::string path, std::string bundle_id, PluginMeta meta) :
    m_path(std::move(path)),
    m_bundle_id(std::move(bundle_id)),
    m_meta(std::move(meta))
{}

sol::object Plugin::execute(Biz::Lua::LuaEngine& lua, const PluginParamValueMap& params) const
{
    SafeFileResolver resolver{m_path};
    lua.set_path_resolver(resolver);

    try {
        lua.run_file(m_path);
    } catch (Biz::Lua::LuaException& e) {
        throw;
    } catch (std::exception& e) {
        throw Biz::Lua::LuaException{e.what(), m_path};
    }

    sol::table opts = lua.state().create_table();
    for (const auto& [name, value] : params) {
        opts[name] = value;
    }
    sol::protected_function fn = lua.state()["execute"];
    sol::object result;
    const sol::protected_function_result ret = fn(opts);
    lua.set_path_resolver(nullptr);
    if (!ret.valid()) {
        const sol::error err = ret;
        throw Biz::Lua::LuaException{err.what(), m_path};
    }
    result = ret;

    Biz::Platform::PlatformServices::instance().render_request_handler().request_render();

    return result;
}

}
