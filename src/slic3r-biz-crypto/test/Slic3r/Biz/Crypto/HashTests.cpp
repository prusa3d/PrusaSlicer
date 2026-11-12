#include <Slic3r/Biz/Crypto/Hash.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace Slic3r::Biz::Crypto;

TEST_CASE("Hash Tests", "[crypto]")
{
    REQUIRE(
        compute_hash(as_bytes_view("Hello\n"), HashType::SHA_1).hex_string()
        == "1d229271928d3f9e2bb0375bd6ce5db6c6d348d9"
    );

    REQUIRE(
        compute_hash(as_bytes_view("Hello\n"), HashType::SHA_256).hex_string()
        == "66a045b452102c59d840ec097d59d9467e13a3f34f6494e539ffd32c1bb35f18"
    );

    REQUIRE(
        compute_hash(as_bytes_view("Hello\n"), HashType::SHA_512).hex_string()
        == "c2bad2223811194582af4d1508ac02cd69eeeeedeeb98d54fcae4dcefb13cc882e7640328206603d3fb9cd5f949a9be0db054dd34fbfa190c498a5fe09750cef"
    );

}
