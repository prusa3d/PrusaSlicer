#pragma once

#include <boost/filesystem/path.hpp>

namespace Slic3r::Biz::Platform {

class IAppConfigProvider {
public:
  virtual ~IAppConfigProvider() = default;
  virtual boost::filesystem::path download_dir() const = 0;

  virtual bool get_show_step_import_parameters() const = 0;
  virtual void set_show_step_import_parameters(bool show) = 0;
  virtual double get_step_linear_precision() const = 0;
  virtual void set_step_linear_precision(double precision) = 0;
  virtual double get_step_angle_precision() const = 0;
  virtual void set_step_angle_precision(double precision) = 0;
};

} // namespace Slic3r::Biz::Platform
