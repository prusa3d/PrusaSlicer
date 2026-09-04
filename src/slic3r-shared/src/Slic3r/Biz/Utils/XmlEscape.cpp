#include "Slic3r/Biz/Utils/XmlEscape.hpp"

namespace Slic3r::Biz::Utils {

//FIXME this has potentially O(n^2) time complexity!
std::string xml_escape(std::string text, bool is_marked/* = false*/)
{
    std::string::size_type pos = 0;
    for (;;)
    {
        pos = text.find_first_of("\"\'&<>", pos);
        if (pos == std::string::npos)
            break;

        std::string replacement;
        switch (text[pos])
        {
        case '\"': replacement = "&quot;"; break;
        case '\'': replacement = "&apos;"; break;
        case '&':  replacement = "&amp;";  break;
        case '<':  replacement = is_marked ? "<" :"&lt;"; break;
        case '>': replacement = is_marked ? ">" : "&gt;"; break;
        default: break;
        }

        text.replace(pos, 1, replacement);
        pos += replacement.size();
    }

    return text;
}
}
