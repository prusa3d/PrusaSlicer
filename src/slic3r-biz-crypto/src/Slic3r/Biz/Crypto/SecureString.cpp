#include "Slic3r/Biz/Crypto/SecureString.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <openssl/crypto.h>
#endif

#include <utility>

namespace Slic3r::Biz::Crypto {

void secure_wipe(void* data, std::size_t size) noexcept
{
    if (!data || size == 0)
        return;

#if defined(_WIN32)
    ::SecureZeroMemory(data, size);
#else
    ::OPENSSL_cleanse(data, size);
#endif
}

void SecureString::dispose_string(std::string& str) noexcept
{
    if (!str.empty()) {
        secure_wipe(&str[0], str.size());
    }
    str.clear();
}

SecureString::SecureString(std::string&& str) noexcept : m_str(std::move(str))
{
    // For a heap-allocated source the move stole the buffer and `str` is
    // already empty; for an SSO source the bytes were copied and the original
    // still holds them. This covers both.
    dispose_string(str);
}

SecureString::SecureString(const char* data, std::size_t size)
{
    m_str.reserve(size);
    m_str.assign(data, size);
}

SecureString::SecureString(SecureString&& other) noexcept : m_str(std::move(other.m_str))
{
    dispose_string(other.m_str);
}

SecureString& SecureString::operator=(SecureString&& other) noexcept
{
    if (this != &other) {
        dispose_string(m_str); // wipe what we're about to drop
        m_str = std::move(other.m_str);
        dispose_string(other.m_str);
    }
    return *this;
}

} // namespace Slic3r::Biz::Crypto
