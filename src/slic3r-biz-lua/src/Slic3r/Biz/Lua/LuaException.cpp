#include <utility>

#include "Slic3r/Biz/Lua/LuaException.hpp"

namespace Slic3r::Biz::Lua {

LuaException::LuaException(const std::string& what) : std::runtime_error(what) {}

LuaException::LuaException(const std::string& what, std::string script_path) :
    std::runtime_error(what),
    m_script_path(std::move(script_path))
{}

const std::string& LuaException::script_path() const
{
    return m_script_path;
}

void LuaException::set_script_path(const std::string& path)
{
    m_script_path = path;
}
} // namespace Slic3r::Biz::Lua
