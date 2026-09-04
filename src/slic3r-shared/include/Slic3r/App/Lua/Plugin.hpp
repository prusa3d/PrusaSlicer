#pragma once

#include <variant>
#include <string>
#include <string_view>
#include <map>

#include <boost/filesystem/path.hpp>
#include <tl/expected.hpp>

#include "Slic3r/Biz/Lua/LuaEngine.hpp"

namespace Slic3r::App::Lua {

enum class PluginType
{
    ProjectPlugin
};

tl::expected<PluginType, std::string> parse_plugin_type(std::string_view s);
std::string to_string(PluginType type);

using PluginParamValue = std::variant<bool, int, double, std::string>;
using PluginParamValueMap = std::map<std::string, PluginParamValue>;

struct PluginParamDef
{
    std::string name;
    std::string label;
    std::string type;
    std::optional<PluginParamValue> default_value;
};

using PluginParamDefs = std::vector<PluginParamDef>;

struct PluginMeta
{
    std::string id;
    PluginType type;
    std::optional<std::string> title;
    std::vector<std::string> menu;
    PluginParamDefs params;
};


class Plugin
{
public:
    const PluginMeta& meta() const { return m_meta; }
    PluginMeta& meta() { return m_meta; }
    const std::string& path() const { return m_path; }

    void execute(Biz::Lua::LuaEngine& lua, const PluginParamValueMap& params) const;

    using ParseResult = tl::expected<Plugin, std::string>;
    static ParseResult
    parse(Biz::Lua::LuaEngine& lua, const std::string& id_prefix, const std::string& path);

private:
    Plugin(std::string  path, PluginMeta  meta);

private:
    std::string m_path;
    PluginMeta m_meta;
};

bool is_path_in_sandbox(
    const boost::filesystem::path& sandbox_path,
    const boost::filesystem::path& tested_path
);

}