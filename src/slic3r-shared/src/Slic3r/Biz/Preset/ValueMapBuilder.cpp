#include "Slic3r/Biz/Preset/ValueMapBuilder.hpp"

namespace Slic3r::Biz::Preset {

void append_value(Expr::ValueMap& values, const char* prefix, const Domain::Preset::HwModel& v)
{
    values[prefix + std::string("model")] = v.model;
    values[prefix + std::string("base_model")] = v.base_model;

}

void append_values(Expr::ValueMap& values, const char* prefix, const Domain::Preset::FeatureValueMap& features)
{
    for (const auto& [k, v] : features)
        append_value(values, prefix + k, v);
}

void append_printer_values(Expr::ValueMap& values, const Domain::Preset::HwPrinterConfig& printer)
{
    append_value(values, "printer.", printer.model);
    append_values(values, "printer.", printer.features);
    append_value(values, "printer.tool_count", printer.tool_count);

    if (printer.feeders.empty()) {
        append_value(values, "feeder.", Domain::Preset::HwModel{});
    } else {
        // At the moment only single feeder is supported
        ASSERT(printer.feeders.size() == 1);

        const auto& feeder = printer.feeders.begin()->second;
        append_value(values, "feeder.", feeder.model);
        // TODO: proper setting
        append_value(values, "feeder.single_mode", false);
    }

    append_sheet_values(values, printer.sheet);
}

void append_print_values(Expr::ValueMap& values, const Domain::ConfigBox& print_preset)
{
    auto it = print_preset.find("layer_height");
    if (it.item)
        append_value(values, "print.layer_height", it.item->value().get<double>());
}

void append_tool_values(Expr::ValueMap& values, const Domain::Preset::HwToolConfig& tool)
{
    append_values(values, "tool.", tool.features);
}

void append_sheet_values(Expr::ValueMap& values, const Domain::Preset::HwSheetConfig& sheet)
{
    append_values(values, "sheet.", sheet.features);
    append_value(values, "sheet.type", sheet.type);
}

}


