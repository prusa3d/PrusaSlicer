#include "Slic3r/Biz/Crypto/Sign.hpp"
#include "Slic3r/TestData.hpp"

#include <catch2/catch_test_macros.hpp>

using Slic3r::get_datadir;
using namespace Slic3r::Biz::Crypto;

TEST_CASE("Sign Tests", "[crypto]")
{
    SECTION("Round trip sign & verify")
    {
        KeyPair key = KeyPair::generate();

        auto pub = key.save_public_key_pem();
        auto priv = key.save_private_key_pem();

        std::string data = "Hello world\n";

        auto sign_key = KeyPair::load_priv_pem(as_bytes_view(priv.view()));
        auto sign_builder = create_signature_builder(HashType::SHA_256, sign_key);
        sign_builder->update(as_bytes_view(data));
        auto sign = sign_builder->finalize();

        REQUIRE(sign.valid());

        auto verify_key = KeyPair::load_pub_pem(as_bytes_view(pub));
        auto sign_verifier = create_signature_verifier(HashType::SHA_256, verify_key);
        sign_verifier->update(as_bytes_view(data));
        bool verified = sign_verifier->finalize(sign);

        REQUIRE(verified);
    }

    SECTION("Verify external data", "[crypto]")
    {
        auto pub_pem_data =
            file_as_bytes((get_datadir() / "dir-source" / "public_key.pem").string());
        Signature sign{file_as_bytes((get_datadir() / "dir-source" / "data.txt.sig").string())};

        KeyPair key = KeyPair::load_pub_pem(pub_pem_data);

        auto verifier = create_signature_verifier(HashType::SHA_256, key);
        file_stream((get_datadir() / "dir-source" / "data.txt").string(), *verifier);
        bool verified = verifier->finalize(sign);

        REQUIRE(verified);
    }
}