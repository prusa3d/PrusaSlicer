#pragma once

#include "Slic3r/Domain/Config.hpp"

namespace Slic3r::Domain {

// Then, define all types of ConfigBoxes that will be used. Provide our list
// of definitions and the type of the box (which must match definitions).

class SLAPrintSettings : public ConfigBox
{
public:
    SLAPrintSettings();
};
class SLAMaterialSettings : public ConfigBox
{
public:
    SLAMaterialSettings();
};
class SLAPrinterSettings : public ConfigBox
{
public:
    SLAPrinterSettings();
};
class SLAObjectSettings : public ConfigBox
{
public:
    SLAObjectSettings();
};

} // namespace Slic3r::Domain
