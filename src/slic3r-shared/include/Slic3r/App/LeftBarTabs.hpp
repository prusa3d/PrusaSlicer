#pragma once

namespace Slic3r::App {

// The order of the items defines the tab order in the left bar.
enum class LeftBarTabs {
    Slicing = 1000,
    //Projects,
    Printers,
    Printables,
    PhysicalPrinter
};
}