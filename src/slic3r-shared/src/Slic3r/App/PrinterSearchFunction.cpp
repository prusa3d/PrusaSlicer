#include "Slic3r/App/PrinterSearchFunction.hpp"

namespace Slic3r::App {

int score_preset_item(
    std::function<const std::string&(const std::string&)> process_string,
    const Biz::Preset::PresetItem& item,
    const std::string& search_text
)
{
    int score = 0;

    const std::string& name = process_string(item.name);
    if (!name.empty() && name.find(search_text) != std::string::npos) {
        score += 10;
    }

    const std::string& hw_printer_config_name = process_string(item.hw_printer_config_name);
    if (!hw_printer_config_name.empty()
        && hw_printer_config_name.find(search_text) != std::string::npos)
    {
        score += 10;
    }

    return score;
}

int score_physical_printer(
    std::function<const std::string&(const std::string&)> process_string,
    const Biz::PhysicalPrinter::PhysicalPrinterConfig& item,
    const std::string& search_text
)
{
    int score = 0;

    const std::string& name = process_string(item.name);
    if (!name.empty() && name.find(search_text) != std::string::npos) {
        score += 10;
    }

    if (item.hw_config.has_value()) {
        const std::string& model_name = process_string(item.hw_config->name);
        if (!model_name.empty() && model_name.find(search_text) != std::string::npos) {
            score += 10;
        }
    }

    const std::string& service_name =
        process_string(Biz::PhysicalPrinter::physical_printer_type_to_string(item));
    if (!service_name.empty() && service_name.find(search_text) != std::string::npos) {
        score += 5;
    }

    const std::string& host = process_string(item.host);
    if (!host.empty() && host.find(search_text) != std::string::npos) {
        score += 5;
    }

    return score;
}

} // namespace Slic3r::App
