#include "Slic3r/Biz/Lua/LuaEngine.hpp"

#include "Slic3r/Biz/Lua/LuaException.hpp"

#include <boost/nowide/fstream.hpp>
#include <fmt/format.h>

namespace Slic3r::Biz::Lua {

namespace {
void ban_keys(sol::state& state, std::initializer_list<std::string_view> keys)
{
    for (const auto key : keys) {
        state[key] = sol::lua_nil;
    }
}
}

LuaEngine::LuaEngine()
{
    m_state.open_libraries(sol::lib::base, sol::lib::table, sol::lib::math, sol::lib::string);
    ban_keys(m_state, {"dofile", "loadfile", "load"});
    sol::function orig_setmetatable = m_state["setmetatable"];
    sol::function orig_getmetatable = m_state["getmetatable"];
    m_state["setmetatable"] =
        [orig_setmetatable](const sol::object& target, const sol::object& new_meta)
    {
        if (target.get_type() != sol::type::table) {
            throw sol::error("Invalid metadata target");
        }
        return orig_setmetatable(target, new_meta);
    };
    m_state["getmetatable"] =
        [orig_getmetatable](const sol::object& target)
        {
            if (target.get_type() != sol::type::table) {
                throw sol::error("Invalid metadata target");
            }
            return orig_getmetatable(target);
        };

}

void LuaEngine::open_registry(const LuaRegistry& registry)
{
    registry(*this);
}

sol::protected_function_result LuaEngine::run_script(const std::string& source)
{
    return m_state.script(source);
}

sol::protected_function_result LuaEngine::run_file(const std::string& file_path)
{
    boost::nowide::ifstream file(file_path);
    if (!file.is_open()) {
        throw LuaException(fmt::format("Could not open file: {}", file_path));
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    // prepend @ to file_path so lua error reporting will treat this as file
    sol::load_result loaded_chunk = m_state.load(buffer.str(), "@" + file_path);
    if (!loaded_chunk.valid()) {
        sol::error err = loaded_chunk;
        throw LuaException(
            fmt::format("Syntax Error in module '%s': %s", file_path.c_str(), err.what())
        );
    }

    sol::protected_function_result result = loaded_chunk();
    return result;
}

std::string LuaEngine::resolve_file(const std::string& path) const
{
    return m_path_resolver ? m_path_resolver(path) : "";
}

const LuaEngine::FilePathResolveFn& LuaEngine::path_resolver() const
{
    return m_path_resolver;
}

void LuaEngine::set_path_resolver(FilePathResolveFn&& resolver)
{
    m_path_resolver = resolver;
}

} // namespace Slic3r::Biz::Lua
