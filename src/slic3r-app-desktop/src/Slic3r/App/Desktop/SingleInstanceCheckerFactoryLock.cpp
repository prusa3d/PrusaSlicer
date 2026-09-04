#include "SingleInstanceCheckerFactory.hpp"

#include "Slic3r/Biz/AppInstance/SingleInstanceCheckerLock.hpp"

namespace Slic3r::App::Desktop::AppInstance::SingleInstanceCheckerFactory {

std::unique_ptr<Biz::Platform::ISingleInstanceChecker> create_single_instance_checker(const boost::filesystem::path& path)
{
    return std::make_unique<Biz::AppInstance::SingleInstanceCheckerLock>(path.filename().string() , path.parent_path().string());
}

}