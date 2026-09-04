#include "SecretStoreFactory.hpp"

#include "Slic3r/Biz/Platform/SecretStoreLinux.hpp"

namespace  Slic3r::App::Desktop::SecretStoreFactory {

std::unique_ptr<Biz::Platform::ISecretStore> create_secret_store()
{
    return std::make_unique<Biz::Platform::SecretStoreLinux>();
}
}