#pragma once

#include "Slic3r/Biz/Platform/ISecretStore.hpp"

#include <memory>
namespace  Slic3r::App::Desktop::SecretStoreFactory {

    std::unique_ptr<Biz::Platform::ISecretStore> create_secret_store();
}