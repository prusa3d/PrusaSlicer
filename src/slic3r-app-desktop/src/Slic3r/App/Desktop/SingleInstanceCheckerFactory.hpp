#pragma once

#include "Slic3r/Biz/Platform/ISingleInstanceChecker.hpp"

#include <memory>
#include <string>
#include <boost/filesystem/path.hpp>

namespace  Slic3r::App::Desktop::AppInstance::SingleInstanceCheckerFactory {
    
    std::unique_ptr<Biz::Platform::ISingleInstanceChecker> create_single_instance_checker(const boost::filesystem::path& path);
}