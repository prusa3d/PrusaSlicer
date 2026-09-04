#pragma once

#include <string>
#include <vector>

namespace Slic3r::Domain::CustomGCode {
enum class Type
{
    ColorChange,
    PausePrint,
    ToolChange,
    Template,
    Custom
};

struct Item
{
    bool operator<(const CustomGCode::Item& rhs) const { return this->print_z < rhs.print_z; }
    bool operator==(const CustomGCode::Item& rhs) const
    {
        return (rhs.print_z   == this->print_z    ) &&
               (rhs.type      == this->type       ) &&
               (rhs.extruder  == this->extruder   ) &&
               (rhs.color     == this->color      ) &&
               (rhs.extra     == this->extra      );
    }
    bool operator!=(const CustomGCode::Item& rhs) const { return ! (*this == rhs); }

    double      print_z;
    CustomGCode::Type type;
    int         extruder;   // Informative value for ColorChangeCode and ToolChangeCode
                            // "gcode" == ColorChangeCode   => M600 will be applied for "extruder" extruder
                            // "gcode" == ToolChangeCode    => for whole print tool will be switched to "extruder" extruder
    std::string color;      // if gcode is equal to PausePrintCode, 
                            // this field is used for save a short message shown on Printer display 
    std::string extra;      // this field is used for the extra data like :
                            // - G-code text for the Type::Custom 
                            // - message text for the Type::PausePrint
};

enum class Mode
{
    Undef,
    SingleExtruder,   // Single extruder printer preset is selected
    MultiAsSingle,    // Multiple extruder printer preset is selected, but 
                      // this mode works just for Single extruder print 
                      // (The same extruder is assigned to all ModelObjects and ModelVolumes).
    MultiExtruder     // Multiple extruder printer preset is selected
};

struct Info
{
    Mode mode = Mode::Undef;
    std::vector<Item> gcodes;

    bool operator==(const Info& rhs) const
    {
        if (rhs.gcodes.empty() && this->gcodes.empty())
            return true; // don't respect to the comparison of the mode, when g_codes are empty
        return  (rhs.mode   == this->mode   ) &&
                (rhs.gcodes == this->gcodes );
    }
    bool operator!=(const Info& rhs) const { return !(*this == rhs); }
};

}
