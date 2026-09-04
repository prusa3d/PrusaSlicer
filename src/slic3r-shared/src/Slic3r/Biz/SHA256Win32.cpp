#include "Slic3r/Biz/SHA256.hpp"

#include <windows.h>
#include <wincrypt.h>

namespace Slic3r::Biz {

std::string sha256(const std::string& input)
{
	HCRYPTPROV          prov_handle = NULL;
    HCRYPTHASH          hash_handle = NULL;
    DWORD               hash_size = 0;
    DWORD               buffer_size = sizeof(DWORD);
    std::string         output;

    if (!CryptAcquireContext(&prov_handle, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        throw std::exception("CryptAcquireContext failed.");
    }
    if (!CryptCreateHash(prov_handle, CALG_SHA_256, 0, 0, &hash_handle)) {
        CryptReleaseContext(prov_handle, 0);
        throw std::exception("CryptCreateHash failed.");
    }
    if (!CryptHashData(hash_handle, reinterpret_cast<const BYTE*>(input.c_str()), input.length(), 0)) {
        CryptDestroyHash(hash_handle);
        CryptReleaseContext(prov_handle, 0);
        throw std::exception("CryptCreateHash failed.");
    }
    if (!CryptGetHashParam(hash_handle, HP_HASHSIZE, reinterpret_cast<BYTE*>(&hash_size), &buffer_size, 0)) {
        CryptDestroyHash(hash_handle);
        CryptReleaseContext(prov_handle, 0);
        throw std::exception("CryptGetHashParam HP_HASHSIZE failed.");
    }
    output.resize(hash_size);
    if (!CryptGetHashParam(hash_handle, HP_HASHVAL, reinterpret_cast<BYTE*>(&output[0]), &hash_size, 0)) {
        CryptDestroyHash(hash_handle);
        CryptReleaseContext(prov_handle, 0);
        throw std::exception("CryptGetHashParam HP_HASHVAL failed.");
    }
    return output;
}

}