#pragma once

#include "Slic3r/Biz/Platform/IAppConfigProvider.hpp"

namespace Slic3r::App {

class AppConfigProvider : public Biz::Platform::IAppConfigProvider {
public:
    boost::filesystem::path download_dir() const override;
    bool get_show_step_import_parameters() const override;
    void set_show_step_import_parameters(bool show) override;
    double get_step_linear_precision() const override;
    void set_step_linear_precision(double precision) override;
    double get_step_angle_precision() const override;
    void set_step_angle_precision(double precision) override;
};

} // namespace Slic3r::App
