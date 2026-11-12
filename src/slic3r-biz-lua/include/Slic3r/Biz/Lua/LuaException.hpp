#pragma once
#include <stdexcept>
#include <string>

namespace Slic3r::Biz::Lua {

class LuaException : public std::runtime_error
{
public:
    explicit LuaException(const std::string& what);
    LuaException(const std::string& what, std::string script_path);

    [[nodiscard]] const std::string& script_path() const;
    void set_script_path(const std::string& path);

private:
    std::string m_script_path;
};

}
