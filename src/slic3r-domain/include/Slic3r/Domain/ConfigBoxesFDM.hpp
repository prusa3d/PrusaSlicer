#pragma once

#include "Slic3r/Domain/Config.hpp"

namespace Slic3r::Domain {

// Then, define all types of ConfigBoxes that will be used. Provide our list
// of definitions and the type of the box (which must match definitions).

class PrintSettings : public ConfigBox
{
public:
    PrintSettings();
};
class FilamentSettings : public ConfigBox
{
public:
    FilamentSettings();
};
class PrinterSettings : public ConfigBox
{
public:
    PrinterSettings();
};
class ToolPrintSettings : public ConfigBox
{
public:
    ToolPrintSettings();
};
class ObjectSettings : public ConfigBox
{
public:
    ObjectSettings();
};
class VolumeSettings : public ConfigBox
{
public:
    VolumeSettings();
};
class ProjectSettings : public ConfigBox
{
public:
    ProjectSettings();
};

} // namespace Slic3r::Domain
