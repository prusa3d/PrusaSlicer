#include "Slic3r/App/Lua/PrintModePlugins.hpp"

#include "Slic3r/App/Lua/PluginSystem.hpp"
#include "Slic3r/Biz/Lua/LuaEngine.hpp"
#include "Slic3r/Biz/Lua/LuaException.hpp"
#include "Slic3r/Log.hpp"

#include <fmt/format.h>

namespace Slic3r::App::Lua {

namespace {

sol::object to_lua(sol::state& lua, const Domain::ConfigValue& value)
{
    return value.visit(Domain::overloaded{
        [&](const auto& scalar) -> sol::object { return sol::make_object(lua, scalar); },
        [&](std::optional<int> v) -> sol::object
        {
            return v ? sol::make_object(lua, *v) : sol::make_object(lua, sol::lua_nil);
        },
        [&](const Domain::Percentage& v) -> sol::object { return sol::make_object(lua, v.value); },
        [&](const Domain::FloatOrPercentage& v) -> sol::object
        {
            return sol::make_object(lua, v.float_value());
        },
        [&](const Domain::Vec2d& v) -> sol::object
        {
            return lua.create_table_with("x", v.x(), "y", v.y());
        },
        [&](const Domain::EnumWrapper& v) -> sol::object
        {
            return sol::make_object(lua, std::string{v.get_string()});
        },
        [&](const Domain::EnumVectorWrapper& v) -> sol::object
        {
            const std::vector<std::string_view> names{v.get_strings()};
            return sol::make_object(
                lua, sol::as_table(std::vector<std::string>(names.begin(), names.end())));
        },
        [&]<typename T>(const std::vector<T>& values) -> sol::object
        {
            sol::table table{lua.create_table()};
            for (size_t i = 0; i < values.size(); ++i) {
                table[i + 1] = to_lua(lua, Domain::ConfigValue{T{values[i]}});
            }
            return table;
        },
    });
}

Domain::PrintModeParam default_value(const PluginParamDef& def)
{
    if (def.default_value) {
        return *def.default_value;
    }
    if (def.type == "bool") {
        return false;
    }
    if (def.type == "double") {
        return 0.;
    }
    if (def.type == "string") {
        return std::string{};
    }
    return 0;
}

/// Every parameter the plugin declares: the given value when present and of the declared type,
/// the manifest default otherwise. An int is accepted for a double parameter.
Domain::PrintModeParams complete_params(const PluginMeta& meta, const Domain::PrintModeParams& given)
{
    Domain::PrintModeParams result;
    for (const PluginParamDef& def : meta.params) {
        Domain::PrintModeParam value{default_value(def)};
        if (const auto it{given.find(def.name)}; it != given.end()) {
            if (it->second.index() == value.index()) {
                value = it->second;
            } else if (std::holds_alternative<double>(value) && std::holds_alternative<int>(it->second)) {
                value = static_cast<double>(std::get<int>(it->second));
            } else {
                SPDLOG_WARN(
                    "Print mode '{}' parameter '{}' has a wrong type, using the default",
                    meta.id, def.name);
            }
        }
        result.insert_or_assign(def.name, std::move(value));
    }
    return result;
}

// The box item decides the type, so a scalar override of a per tool key is just a scalar.
void set_override(Domain::ConfigOverrides& overrides, const std::string& key, const sol::object& value)
{
    const Domain::ConfigItem* item{overrides.find(key)};
    if (!item) {
        SPDLOG_ERROR("Print mode override '{}' is not a macro overridable key", key);
        return;
    }
    item->value().visit(Domain::overloaded{
        [&](bool) { overrides.set(key, value.as<bool>()); },
        [&](int) { overrides.set(key, value.as<int>()); },
        [&](double) { overrides.set(key, value.as<double>()); },
        [&](const std::string&) { overrides.set(key, value.as<std::string>()); },
        [&](const Domain::EnumWrapper&) { overrides.set(key, value.as<std::string>()); },
        [&](const Domain::Percentage&) { overrides.set(key, Domain::Percentage{value.as<double>()}); },
        [&](const Domain::FloatOrPercentage&)
        {
            overrides.set(key, Domain::FloatOrPercentage{value.as<double>()});
        },
        [&](const auto&) { SPDLOG_ERROR("Print mode override '{}' has an unsupported type", key); },
    });
}

} // namespace

PrintModePlugins::PrintModePlugins(
    PluginSystem& plugin_system, Biz::PrintModeInteractor& interactor) :
    m_plugin_system(plugin_system),
    m_interactor(interactor),
    m_rescan_scope(plugin_system, *this)
{
    m_interactor.set_provider(this);
}

PrintModePlugins::~PrintModePlugins()
{
    m_interactor.set_provider(nullptr);
}

std::vector<std::string> PrintModePlugins::modes() const
{
    std::vector<std::string> result;
    for (const Plugin& plugin : m_plugin_system.plugins() | std::views::values) {
        if (plugin.meta().type == PluginType::PrintMode) {
            result.push_back(plugin.meta().id);
        }
    }
    return result;
}

Biz::PrintMode PrintModePlugins::evaluate(
    const std::string& id,
    const std::map<std::string, Domain::ConfigValue>& inputs,
    const Domain::PrintModeParams& params
) const
{
    const auto it{m_plugin_system.plugins().find(id)};
    if (it == m_plugin_system.plugins().end()) {
        return {{}, fmt::format("Print mode plugin '{}' not found", id)};
    }
    const PluginMeta& meta{it->second.meta()};

    Biz::Lua::LuaEngine lua;
    sol::state& state{lua.state()};
    sol::table config{state.create_table()};
    config.set_function(
        "value",
        [&inputs, &state](const sol::object&, const std::string& key) -> sol::object
        {
            const auto input{inputs.find(key)};
            if (input == inputs.end()) {
                throw Biz::Lua::LuaException(fmt::format("Unknown config key '{}'", key));
            }
            return to_lua(state, input->second);
        });
    state.create_named_table("api")["config"] = config;

    Biz::PrintMode mode;
    mode.layer.name = meta.title.value_or(meta.id);
    mode.layer.params = complete_params(meta, params);

    try {
        // PluginParamValueMap and Domain::PrintModeParams are the same variant map.
        const sol::object result{it->second.execute(lua, mode.layer.params)};
        if (!result.is<sol::table>()) {
            mode.error = "The print mode did not return a table";
        } else {
            result.as<sol::table>().for_each(
                [&](const sol::object& key, const sol::object& value) {
                    set_override(mode.layer.overrides.overrides, key.as<std::string>(), value);
                });
        }
    } catch (const std::exception& e) {
        mode.error = e.what();
    }
    if (mode.error) {
        SPDLOG_ERROR("Print mode '{}' failed: {}", id, *mode.error);
        mode.layer.overrides = {};
    }
    return mode;
}

void PrintModePlugins::on_plugins_scanned(const PluginRegistry& /*registry*/)
{
    m_interactor.reload_provider();
}

} // namespace Slic3r::App::Lua
