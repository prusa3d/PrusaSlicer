#include "Slic3r/Biz/Network/ServerTrust.hpp"

#include "Slic3r/Biz/Network/Tls.hpp"

#include <optional>

namespace Slic3r::Biz::Network {

bool ServerTrust::accepts_chain(const std::vector<std::string>& chain, std::string_view host) const
{
    if (chain.empty()) {
        return false;
    }
    switch (kind) {
    case ServerTrustKind::PinnedKey: {
        const std::optional<std::string> leaf_pin = Tls::spki_pin(chain.front());
        return leaf_pin && !pin.empty() && *leaf_pin == pin;
    }
    case ServerTrustKind::Certificate:
        return !pem.empty() && Tls::chain_verifies(chain, pem, host);
    case ServerTrustKind::System:
    default:
        return false;
    }
}

} // namespace Slic3r::Biz::Network
