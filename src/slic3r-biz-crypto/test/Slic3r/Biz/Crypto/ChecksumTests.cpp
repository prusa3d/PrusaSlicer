#include "Slic3r/Biz/Crypto/Checksum.hpp"
#include "Slic3r/TestData.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace Slic3r::Biz::Crypto;

TEST_CASE("Checksum tests", "[crypto]")
{
    auto base_dir = Slic3r::get_datadir();
    using Factory = std::function<IContentProviderPtr()>;
    auto content_provider_factory = GENERATE_COPY(
        Factory {[base_dir] { return create_directory_source((base_dir / "dir-source").string()); }},
        Factory {[base_dir] { return create_zip_source((base_dir / "zip-source.zip").string()); }}
    );
    auto content_provider = content_provider_factory();
    DirChecksum checksum  = DirChecksum::load_from_file(
        *content_provider,
        "checksum.sha256",
        HashType::SHA_256
    );

    SECTION("non-strict succeeds if all mentioned files verify")
    {
        ChecksumVerifyOpts opts{.strict = false};
        bool verified = checksum.verify(*content_provider, opts);
        REQUIRE(verified);
    }

    SECTION("non-strict fails if some mentioned file is missing")
    {
        ChecksumVerifyOpts opts{.strict = false};
        auto my_checksum = checksum;
        my_checksum.entries.emplace_back(
            compute_hash(as_bytes_view(std::string{""}), HashType::SHA_256),
            "some_file.txt"
        );
        bool verified = my_checksum.verify(*content_provider, opts);
        REQUIRE(!verified);
    }


    SECTION("non-strict fails if some mentioned files don't verify")
    {
        ChecksumVerifyOpts opts{.strict = false};
        auto my_checksum = checksum;

        for (auto& e : my_checksum.entries) {
            auto raw_bytes = e.hash.bytes();
            // flip some bits
            raw_bytes.at(0) ^= 0x7f;
            e.hash = Hash{e.hash.type(), raw_bytes};
        }

        bool verified = my_checksum.verify(*content_provider, opts);
        REQUIRE(!verified);
    }

    SECTION("strict fails if non-whitelisted file wasn't verified")
    {
        ChecksumVerifyOpts opts{.strict = true};
        bool verified = checksum.verify(*content_provider, opts);
        REQUIRE(!verified);
    }

    SECTION("strict with whitelist succeeds")
    {
        ChecksumVerifyOpts opts{
            .strict    = true,
            .whitelist = {
                "checksum.sha256",
                "private_key.pem",
                "public_key.pem",
            }
        };
        bool verified = checksum.verify(*content_provider, opts);
        REQUIRE(verified);
    }
}
