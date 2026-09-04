#ifndef slic3r_FindReplace_hpp_
#define slic3r_FindReplace_hpp_

#include <boost/regex.hpp>
#include <boost/regex/v5/regex.hpp>
#include <string>
#include <vector>

#include "libslic3r/ConfigViews.hpp"

namespace Slic3r {

class GCodeFindReplace {
public:
    GCodeFindReplace(const PrintConfigView &print_config) : GCodeFindReplace(print_config.get<std::vector<std::string>>("gcode_substitutions")) {}
    GCodeFindReplace(const std::vector<std::string> &gcode_substitutions);


    std::string process_layer(const std::string &gcode);
    
private:
    struct Substitution {
        std::string     plain_pattern;
        boost::regex    regexp_pattern;
        std::string     format;

        bool            regexp { false };
        bool            case_insensitive { false };
        bool            whole_word { false };
        // Valid for regexp only. Equivalent to Perl's /s modifier.
        bool            single_line { false };
    };
    std::vector<Substitution> m_substitutions;
};

}

#endif // slic3r_FindReplace_hpp_
