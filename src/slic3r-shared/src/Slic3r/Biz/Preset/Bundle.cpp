#include "Slic3r/Biz/Preset/Bundle.hpp"

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/directory.hpp>

#include "Slic3r/Biz/Preset/Loader/HwConfigLoader.hpp"
#include "Slic3r/Biz/Preset/Loader/PresetLoader.hpp"

namespace Slic3r::Biz::Preset {

namespace fs = boost::filesystem;

void Bundle::load_bundles(const std::string& base_dir)
{
    fs::path base_dir_path{base_dir};
    for (const auto& p : fs::directory_iterator(base_dir_path)) {
        if (!p.is_directory())
            continue;
        fs::path vendor_path = p / "vendor.yaml";
        if (!fs::directory_entry{vendor_path}.exists())
            continue;

        VendorBundle bundle;
        IO::HwConfigLoader config_loader;
        config_loader.load(vendor_path.string());

    }
}


}
