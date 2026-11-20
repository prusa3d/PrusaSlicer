#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace Slic3r::Biz {

/**
 * @brief The value as a JavaScript literal, to be put into a script: ASCII only (no line
 * separators that would end a string literal) and invalid UTF-8 replaced instead of throwing.
 */
inline std::string json_for_script(const nlohmann::json& value)
{
    return value.dump(-1, ' ', true, nlohmann::json::error_handler_t::replace);
}

} // namespace Slic3r::Biz
