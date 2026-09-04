#include "SecretStoreFactory.hpp"

#include "Slic3r/Biz/WX/SecretStore.hpp"

namespace  Slic3r::App::Desktop::SecretStoreFactory {

std::unique_ptr<Biz::Platform::ISecretStore> create_secret_store()
{
    return std::make_unique<Biz::WX::SecretStore>();
}
}