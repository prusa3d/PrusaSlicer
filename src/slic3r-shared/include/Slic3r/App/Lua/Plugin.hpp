#pragma once

#include <variant>
#include <string>
#include <string_view>
#include <map>

#include <boost/filesystem/path.hpp>
#include <tl/expected.hpp>

#include "Slic3r/Biz/Lua/LuaEngine.hpp"
#include "Slic3r/Domain/Expr/ExprAst.hpp"
#include "Slic3r/Domain/TextConfiguration.hpp"

namespace Slic3r::App::Lua {

enum class PluginType
{
    ProjectPlugin,
    PrintMode
};

tl::expected<PluginType, std::string> parse_plugin_type(std::string_view s);

using PluginParamValue = std::variant<bool, int, double, std::string, Domain::FontDescriptor>;
using PluginParamValueMap = std::map<std::string, PluginParamValue>;

struct PluginParamDef
{
    std::string name;
    std::string label;
    std::string type;
    std::optional<PluginParamValue> default_value;
    std::vector<std::string> allowed_values;
};

using PluginParamDefs = std::vector<PluginParamDef>;

struct PluginMeta
{
    std::string id;
    PluginType type;
    std::optional<std::string> title;
    std::vector<std::string> menu;
    PluginParamDefs params;
    /// Printer compatibility, in the preset condition language over `printer.*`, `feeder.*` and
    /// `sheet.*` variables. No condition means compatible with every printer.
    std::optional<Domain::Expr::ExprAst> condition;
};


class Plugin
{
public:
    const PluginMeta& meta() const { return m_meta; }
    PluginMeta& meta() { return m_meta; }
    const std::string& path() const { return m_path; }
    const std::string& bundle_id() const { return m_bundle_id; }

    sol::object execute(Biz::Lua::LuaEngine& lua, const PluginParamValueMap& params) const;

    using ParseResult = tl::expected<Plugin, std::string>;
    static ParseResult
    parse(Biz::Lua::LuaEngine& lua, const std::string& bundle_id, const std::string& path);

private:
    Plugin(std::string  path, std::string bundle_id, PluginMeta  meta);

private:
    std::string m_path;
    std::string m_bundle_id;
    PluginMeta m_meta;
};

bool is_path_in_sandbox(
    const boost::filesystem::path& sandbox_path,
    const boost::filesystem::path& tested_path
);

}
