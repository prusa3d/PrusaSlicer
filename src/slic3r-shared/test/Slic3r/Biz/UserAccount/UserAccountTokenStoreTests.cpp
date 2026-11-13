#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountTokenStore.hpp"
#include "Slic3r/TestUtils/MemorySecretStore.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace Slic3r::Biz;
using namespace Slic3r::Biz::UserAccount;

TEST_CASE("Tokens are stored separately per service mode", "[user_account]")
{
    auto store                        = std::make_unique<Tests::MemorySecretStore>();
    Tests::MemorySecretStore* secrets = store.get();
    Platform::PlatformServices::instance().set_secret_store(std::move(store));

    if (!TokenStore::save_tokens({"online-access", "online-refresh", "", "", ""})) {
        SKIP("Token store is not available in this build.");
    }
    REQUIRE(
        TokenStore::
            save_offline_credential({"offline-access", "cred-1", 0, "http://192.168.1.10:8080"})
    );

    CHECK(secrets->secrets.at("PrusaAccount").first == "tokens");
    CHECK(secrets->secrets.at("PrusaAccountOffline").first == "tokens");

    TokenStore::StoreData data;
    REQUIRE(TokenStore::load_tokens(data));
    CHECK(data.access_token == "online-access");

    // Logging out of Prusa Account keeps the Connect Local credential.
    TokenStore::reset();
    REQUIRE(TokenStore::read_offline_credential().credential);
    CHECK(TokenStore::read_offline_credential().credential->token == "offline-access");

    TokenStore::reset_all_modes();
    data = {};
    TokenStore::load_tokens(data);
    CHECK(data.access_token.empty());
    CHECK(TokenStore::read_offline_credential().status == TokenStore::StoreReadStatus::Empty);
}
