#include <boost/locale/message.hpp>

namespace Slic3r::Biz {

static std::locale g_ui_locale = std::locale::classic();

void set_ui_locale(std::locale/*&*/ new_locale)
{
    g_ui_locale = new_locale;
}

std::string _u8(const std::string& s) 
{ 
    return boost::locale::translate(s.c_str()).str(Slic3r::Biz::g_ui_locale);
}

std::string _ctx_u8(const std::string& s, const std::string& ctx)
{ 
    return boost::locale::translate(ctx.c_str(), s.c_str()).str(Slic3r::Biz::g_ui_locale);
}

std::string _L_PLURAL_u8(const std::string& single, const std::string& plural, int n)
{ 
    return boost::locale::translate(single.c_str(), plural.c_str(), n).str(Slic3r::Biz::g_ui_locale);
}

}