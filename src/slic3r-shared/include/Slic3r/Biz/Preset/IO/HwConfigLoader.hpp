#pragma once

#include <Slic3r/Domain/Preset/HwConfig.hpp>

namespace Slic3r::Biz::Preset::IO {

class HwConfigLoader
{
public:
    HwConfigLoader();
    Domain::Preset::VendorData& load(const std::string& filename);
    static Domain::Preset::VendorInfo load_info_only(const std::string& filename);

    const Domain::Preset::VendorData& result() const { return m_result; }
    Domain::Preset::VendorData& result() { return m_result; }

    Domain::Preset::VendorData release()
    {
        Domain::Preset::VendorData vendor = std::move(m_result);
        init_result();
        return vendor;
    }
private:
    void init_result();
private:
    Domain::Preset::VendorData m_result;
};

Domain::Preset::HwPrinterConfigs load_vendor_user_configs(const std::string& dir_path, const Domain::Preset::VendorData& vendor_data);
void save_vendor_user_configs(const Domain::Preset::HwPrinterConfigs& configs, const std::string& dir_path, const Domain::Preset::VendorData& vendor_data);

}
