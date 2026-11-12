#pragma once

#include <string>

namespace Slic3r::Biz {

// Mark, do NOT translate
inline const std::string& L(const std::string& s)
{
    return s;
}

// Mark, do NOT translate
inline const std::string& L_CONTEXT(const std::string& s, const std::string& ctx)
{
    return s;
}

// Translate, do NOT mark.
extern std::string _u8(const std::string& s);

// Mark and translate.
inline std::string _u8L(const std::string& s)
{
    return _u8(s);
}

// Translate, do NOT mark.
extern std::string _ctx_u8(const std::string& s, const std::string& ctx);

// Mark and translate.
inline std::string _ctx_u8L(const std::string& s, const std::string& ctx)
{
    return _ctx_u8(s, ctx);
}

// Mark and translate.
extern std::string _L_PLURAL_u8(const std::string& single, const std::string& plural, int n);

} // namespace Slic3r::Biz
