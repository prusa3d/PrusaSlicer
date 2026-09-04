#pragma once

#include <functional>
#include <string>

#include <lua.hpp>
#include <sol/sol.hpp>


namespace Slic3r::Biz::Lua {
class LuaEngine;

using LuaRegistry = std::function<void(LuaEngine&)>;


class LuaEngine
{
public:
    using FilePathResolveFn = std::function<std::string(const std::string&)>;

    LuaEngine();

    void open_registry(const LuaRegistry& registry);
    sol::protected_function_result run_script(const std::string& source);
    sol::protected_function_result run_file(const std::string& file_path);

    sol::state& state() { return m_state; }
    const sol::state& state() const { return m_state; }

    void set_path_resolver(FilePathResolveFn&& resolver);
    const FilePathResolveFn& path_resolver() const;
    std::string resolve_file(const std::string& path) const;

private:
    sol::state m_state;
    FilePathResolveFn m_path_resolver{nullptr};
};

} // namespace Slic3r::Biz::Lua
