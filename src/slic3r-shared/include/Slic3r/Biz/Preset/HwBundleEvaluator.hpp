#pragma once
#include "Slic3r/Biz/Preset/HwConfigEvaluator.hpp"
#include "Slic3r/Domain/Preset/Bundle.hpp"

namespace Slic3r::Biz::Preset {

class HwBundleEvaluator
{
public:
    explicit HwBundleEvaluator(const Domain::Preset::Bundle& preset_bundle) : m_preset_bundle(preset_bundle) {}

    tl::expected<Domain::Preset::HwPrinterConfig, std::string> evaluate_config(const Domain::Preset::HwPrinterConfig& external_config) const;
private:
    const Domain::Preset::Bundle& m_preset_bundle;
};

} // namespace Slic3r::Biz::Preset
