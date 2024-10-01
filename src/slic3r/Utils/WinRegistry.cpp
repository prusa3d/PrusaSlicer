///|/ Copyright (c) Prusa Research 2022 Enrico Turri @enricoturri1966
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "WinRegistry.hpp"

#ifdef _WIN32
#include <shlobj.h>
#include <wincrypt.h>
#include <winternl.h>
#include <sddl.h>

namespace Slic3r {

// Helper class which automatically closes the handle when
// going out of scope
class AutoHandle
{
    HANDLE m_handle{ nullptr };

public:
    explicit AutoHandle(HANDLE handle) : m_handle(handle) {}
    ~AutoHandle() { if (m_handle != nullptr) ::CloseHandle(m_handle); }
    HANDLE get() { return m_handle; }
};

// Helper class which automatically closes the key when
// going out of scope
class AutoRegKey
{
    HKEY m_key{ nullptr };

public:
    explicit AutoRegKey(HKEY key) : m_key(key) {}
    ~AutoRegKey() { if (m_key != nullptr) ::RegCloseKey(m_key); }
    HKEY get() { return m_key; }
};

// returns true if the given value is set/modified into Windows registry
static bool set_into_win_registry(HKEY hkeyHive, const wchar_t* pszVar, const wchar_t* pszValue)
{
    // see as reference: https://stackoverflow.com/questions/20245262/c-program-needs-an-file-association
    wchar_t szValueCurrent[1000];
    DWORD dwType;
    DWORD dwSize = sizeof(szValueCurrent);

    LSTATUS iRC = ::RegGetValueW(hkeyHive, pszVar, nullptr, RRF_RT_ANY, &dwType, szValueCurrent, &dwSize);

    const bool bDidntExist = iRC == ERROR_FILE_NOT_FOUND;

    if (iRC != ERROR_SUCCESS && !bDidntExist)
        // an error occurred
        return false;

    if (!bDidntExist) {
        if (dwType != REG_SZ)
            // invalid type
            return false;

        if (::wcscmp(szValueCurrent, pszValue) == 0)
            // value already set
            return false;
    }

    DWORD dwDisposition;
    HKEY hkey;
    iRC = ::RegCreateKeyExW(hkeyHive, pszVar, 0, 0, 0, KEY_ALL_ACCESS, nullptr, &hkey, &dwDisposition);
    bool ret = false;
    if (iRC == ERROR_SUCCESS) {
        iRC = ::RegSetValueExW(hkey, L"", 0, REG_SZ, (BYTE*)pszValue, (::wcslen(pszValue) + 1) * sizeof(wchar_t));
        if (iRC == ERROR_SUCCESS)
            ret = true;
    }

    RegCloseKey(hkey);
    return ret;
}

static std::wstring get_current_user_string_sid()
{
    HANDLE rawProcessToken;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY,
        &rawProcessToken))
        return L"";

    AutoHandle processToken(rawProcessToken);

    DWORD userSize = 0;
    if (!(!::GetTokenInformation(processToken.get(), TokenUser, nullptr, 0, &userSize) &&
        GetLastError() == ERROR_INSUFFICIENT_BUFFER))
        return L"";

    std::vector<unsigned char> userBytes(userSize);
    if (!::GetTokenInformation(processToken.get(), TokenUser, userBytes.data(), userSize, &userSize))
        return L"";

    wchar_t* rawSid = nullptr;
    if (!::ConvertSidToStringSidW(reinterpret_cast<PTOKEN_USER>(userBytes.data())->User.Sid, &rawSid))
        return L"";

    return rawSid;
}

/*
 * Create the string which becomes the input to the UserChoice hash.
 *
 * @see generate_user_choice_hash() for parameters.
 *
 * @return The formatted string, empty string on failure.
 *
 * NOTE: This uses the format as of Windows 10 20H2 (latest as of this writing),
 * used at least since 1803.
 * There was at least one older version, not currently supported: On Win10 RTM
 * (build 10240, aka 1507) the hash function is the same, but the timestamp and
 * User Experience string aren't included; instead (for protocols) the string
 * ends with the exe path. The changelog of SetUserFTA suggests the algorithm
 * changed in 1703, so there may be two versions: before 1703, and 1703 to now.
 */
static std::wstring format_user_choice_string(const wchar_t* aExt, const wchar_t* aUserSid, const wchar_t* aProgId, SYSTEMTIME aTimestamp)
{
    aTimestamp.wSecond = 0;
    aTimestamp.wMilliseconds = 0;

    FILETIME fileTime = { 0 };
    if (!::SystemTimeToFileTime(&aTimestamp, &fileTime))
        return L"";

    // This string is built into Windows as part of the UserChoice hash algorithm.
    // It might vary across Windows SKUs (e.g. Windows 10 vs. Windows Server), or
    // across builds of the same SKU, but this is the only currently known
    // version. There isn't any known way of deriving it, so we assume this
    // constant value. If we are wrong, we will not be able to generate correct
    // UserChoice hashes.
    const wchar_t* userExperience =
        L"User Choice set via Windows User Experience "
        L"{D18B6DD5-6124-4341-9318-804003BAFA0B}";

    const wchar_t* userChoiceFmt =
        L"%s%s%s"
        L"%08lx"
        L"%08lx"
        L"%s";

    int userChoiceLen = _scwprintf(userChoiceFmt, aExt, aUserSid, aProgId,
        fileTime.dwHighDateTime, fileTime.dwLowDateTime, userExperience);
    userChoiceLen += 1;  // _scwprintf does not include the terminator

    std::wstring userChoice(userChoiceLen, L'\0');
    _snwprintf_s(userChoice.data(), userChoiceLen, _TRUNCATE, userChoiceFmt, aExt,
        aUserSid, aProgId, fileTime.dwHighDateTime, fileTime.dwLowDateTime, userExperience);

    ::CharLowerW(userChoice.data());
    return userChoice;
}

// @return The MD5 hash of the input, nullptr on failure.
static std::vector<DWORD> cng_md5(const unsigned char* bytes, ULONG bytesLen) {
    constexpr ULONG MD5_BYTES = 16;
    constexpr ULONG MD5_DWORDS = MD5_BYTES / sizeof(DWORD);
    std::vector<DWORD> hash;

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (NT_SUCCESS(::BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_MD5_ALGORITHM, nullptr, 0))) {
        BCRYPT_HASH_HANDLE hHash = nullptr;
        // As of Windows 7 the hash handle will manage its own object buffer when
        // pbHashObject is nullptr and cbHashObject is 0.
        if (NT_SUCCESS(::BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0))) {
            // BCryptHashData promises not to modify pbInput.
            if (NT_SUCCESS(::BCryptHashData(hHash, const_cast<unsigned char*>(bytes), bytesLen, 0))) {
                hash.resize(MD5_DWORDS);
                if (!NT_SUCCESS(::BCryptFinishHash(hHash, reinterpret_cast<unsigned char*>(hash.data()),
                    MD5_DWORDS * sizeof(DWORD), 0)))
                    hash.clear();
            }
            ::BCryptDestroyHash(hHash);
        }
        ::BCryptCloseAlgorithmProvider(hAlg, 0);
    }

    return hash;
}

static inline DWORD word_swap(DWORD v)
{
    return (v >> 16) | (v << 16);
}

// @return The input bytes encoded as base64, nullptr on failure.
static std::wstring crypto_api_base64_encode(const unsigned char* bytes, DWORD bytesLen) {
    DWORD base64Len = 0;
    if (!::CryptBinaryToStringW(bytes, bytesLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &base64Len))
        return L"";

    std::wstring base64(base64Len, L'\0');
    if (!::CryptBinaryToStringW(bytes, bytesLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, base64.data(), &base64Len))
        return L"";

    return base64;
}

/*
 * Generate the UserChoice Hash.
 *
 * This implementation is based on the references listed above.
 * It is organized to show the logic as clearly as possible, but at some
 * point the reasoning is just "this is how it works".
 *
 * @param inputString   A null-terminated string to hash.
 *
 * @return The base64-encoded hash, or empty string on failure.
 */
static std::wstring hash_string(const wchar_t* inputString)
{
    auto inputBytes = reinterpret_cast<const unsigned char*>(inputString);
    int inputByteCount = (::lstrlenW(inputString) + 1) * sizeof(wchar_t);

    constexpr size_t DWORDS_PER_BLOCK = 2;
    constexpr size_t BLOCK_SIZE = sizeof(DWORD) * DWORDS_PER_BLOCK;
    // Incomplete blocks are ignored.
    int blockCount = inputByteCount / BLOCK_SIZE;

    if (blockCount == 0)
        return L"";

    // Compute an MD5 hash. md5[0] and md5[1] will be used as constant multipliers
    // in the scramble below.
    auto md5 = cng_md5(inputBytes, inputByteCount);
    if (md5.empty())
        return L"";

    // The following loop effectively computes two checksums, scrambled like a
    // hash after every DWORD is added.

    // Constant multipliers for the scramble, one set for each DWORD in a block.
    const DWORD C0s[DWORDS_PER_BLOCK][5] = {
        {md5[0] | 1, 0xCF98B111uL, 0x87085B9FuL, 0x12CEB96DuL, 0x257E1D83uL},
        {md5[1] | 1, 0xA27416F5uL, 0xD38396FFuL, 0x7C932B89uL, 0xBFA49F69uL} };
    const DWORD C1s[DWORDS_PER_BLOCK][5] = {
        {md5[0] | 1, 0xEF0569FBuL, 0x689B6B9FuL, 0x79F8A395uL, 0xC3EFEA97uL},
        {md5[1] | 1, 0xC31713DBuL, 0xDDCD1F0FuL, 0x59C3AF2DuL, 0x35BD1EC9uL} };

    // The checksums.
    DWORD h0 = 0;
    DWORD h1 = 0;
    // Accumulated total of the checksum after each DWORD.
    DWORD h0Acc = 0;
    DWORD h1Acc = 0;

    for (int i = 0; i < blockCount; ++i) {
        for (int j = 0; j < DWORDS_PER_BLOCK; ++j) {
            const DWORD* C0 = C0s[j];
            const DWORD* C1 = C1s[j];

            DWORD input;
            memcpy(&input, &inputBytes[(i * DWORDS_PER_BLOCK + j) * sizeof(DWORD)], sizeof(DWORD));

            h0 += input;
            // Scramble 0
            h0 *= C0[0];
            h0 = word_swap(h0) * C0[1];
            h0 = word_swap(h0) * C0[2];
            h0 = word_swap(h0) * C0[3];
            h0 = word_swap(h0) * C0[4];
            h0Acc += h0;

            h1 += input;
            // Scramble 1
            h1 = word_swap(h1) * C1[1] + h1 * C1[0];
            h1 = (h1 >> 16) * C1[2] + h1 * C1[3];
            h1 = word_swap(h1) * C1[4] + h1;
            h1Acc += h1;
        }
    }

    DWORD hash[2] = { h0 ^ h1, h0Acc ^ h1Acc };
    return crypto_api_base64_encode(reinterpret_cast<const unsigned char*>(hash), sizeof(hash));
}

static std::wstring generate_user_choice_hash(const wchar_t* aExt, const wchar_t* aUserSid, const wchar_t* aProgId, SYSTEMTIME aTimestamp)
{
    const std::wstring userChoice = format_user_choice_string(aExt, aUserSid, aProgId, aTimestamp);
    if (userChoice.empty())
        return L"";

    return hash_string(userChoice.c_str());
}

static bool add_milliseconds_to_system_time(SYSTEMTIME& aSystemTime, ULONGLONG aIncrementMS)
{
    FILETIME fileTime;
    ULARGE_INTEGER fileTimeInt;
    if (!::SystemTimeToFileTime(&aSystemTime, &fileTime))
        return false;

    fileTimeInt.LowPart = fileTime.dwLowDateTime;
    fileTimeInt.HighPart = fileTime.dwHighDateTime;

    // FILETIME is in units of 100ns.
    fileTimeInt.QuadPart += aIncrementMS * 1000 * 10;

    fileTime.dwLowDateTime = fileTimeInt.LowPart;
    fileTime.dwHighDateTime = fileTimeInt.HighPart;
    SYSTEMTIME tmpSystemTime;
    if (!::FileTimeToSystemTime(&fileTime, &tmpSystemTime))
        return false;

    aSystemTime = tmpSystemTime;
    return true;
}

// Compare two SYSTEMTIMEs as FILETIME after clearing everything
// below minutes.
static bool check_equal_minutes(SYSTEMTIME aSystemTime1, SYSTEMTIME aSystemTime2)
{
    aSystemTime1.wSecond = 0;
    aSystemTime1.wMilliseconds = 0;

    aSystemTime2.wSecond = 0;
    aSystemTime2.wMilliseconds = 0;

    FILETIME fileTime1;
    FILETIME fileTime2;
    if (!::SystemTimeToFileTime(&aSystemTime1, &fileTime1) || !::SystemTimeToFileTime(&aSystemTime2, &fileTime2))
        return false;

    return (fileTime1.dwLowDateTime == fileTime2.dwLowDateTime) && (fileTime1.dwHighDateTime == fileTime2.dwHighDateTime);
}

static std::wstring get_association_key_path(const wchar_t* aExt)
{
    const wchar_t* keyPathFmt;
    if (aExt[0] == L'.')
        keyPathFmt = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\%s";
    else
        keyPathFmt = L"SOFTWARE\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\%s";

    int keyPathLen = _scwprintf(keyPathFmt, aExt);
    keyPathLen += 1;  // _scwprintf does not include the terminator

    std::wstring keyPath(keyPathLen, '\0');
    _snwprintf_s(keyPath.data(), keyPathLen, _TRUNCATE, keyPathFmt, aExt);
    return keyPath;
}

/*
 * Set an association with a UserChoice key
 *
 * Removes the old key, creates a new one with ProgID and Hash set to
 * enable a new asociation.
 *
 * @param aExt      File type or protocol to associate
 * @param aProgID   ProgID to use for the asociation
 *
 * @return true if successful, false on error.
 */
static bool set_user_choice(const wchar_t* aExt, const wchar_t* aProgID) {

    const std::wstring aSid = get_current_user_string_sid();
    if (aSid.empty())
        return false;

    SYSTEMTIME hashTimestamp;
    ::GetSystemTime(&hashTimestamp);
    std::wstring hash = generate_user_choice_hash(aExt, aSid.c_str(), aProgID, hashTimestamp);
    if (hash.empty())
        return false;

    // The hash changes at the end of each minute, so check that the hash should
    // be the same by the time we're done writing.
    const ULONGLONG kWriteTimingThresholdMilliseconds = 100;
    // Generating the hash could have taken some time, so start from now.
    SYSTEMTIME writeEndTimestamp;
    ::GetSystemTime(&writeEndTimestamp);
    if (!add_milliseconds_to_system_time(writeEndTimestamp, +kWriteTimingThresholdMilliseconds))
        return false;

    if (!check_equal_minutes(hashTimestamp, writeEndTimestamp)) {
        ::Sleep(kWriteTimingThresholdMilliseconds * 2);

        // For consistency, use the current time.
        ::GetSystemTime(&hashTimestamp);
        hash = generate_user_choice_hash(aExt, aSid.c_str(), aProgID, hashTimestamp);
        if (hash.empty())
            return false;
    }

    const std::wstring assocKeyPath = get_association_key_path(aExt);
    if (assocKeyPath.empty())
        return false;

    LSTATUS ls;
    HKEY rawAssocKey;
    ls = ::RegOpenKeyExW(HKEY_CURRENT_USER, assocKeyPath.data(), 0, KEY_READ | KEY_WRITE, &rawAssocKey);
    if (ls != ERROR_SUCCESS)
        return false;

    AutoRegKey assocKey(rawAssocKey);

    HKEY currUserChoiceKey;
    ls = ::RegOpenKeyExW(assocKey.get(), L"UserChoice", 0, KEY_READ, &currUserChoiceKey);
    if (ls == ERROR_SUCCESS) {
        ::RegCloseKey(currUserChoiceKey);
        // When Windows creates this key, it is read-only (Deny Set Value), so we need
        // to delete it first.
        // We don't set any similar special permissions.
        ls = ::RegDeleteKeyW(assocKey.get(), L"UserChoice");
        if (ls != ERROR_SUCCESS)
            return false;
    }

    HKEY rawUserChoiceKey;
    ls = ::RegCreateKeyExW(assocKey.get(), L"UserChoice", 0, nullptr,
        0 /* options */, KEY_READ | KEY_WRITE,
        0 /* security attributes */, &rawUserChoiceKey,
        nullptr);
    if (ls != ERROR_SUCCESS)
        return false;

    AutoRegKey userChoiceKey(rawUserChoiceKey);
    DWORD progIdByteCount = (::lstrlenW(aProgID) + 1) * sizeof(wchar_t);
    ls = ::RegSetValueExW(userChoiceKey.get(), L"ProgID", 0, REG_SZ, reinterpret_cast<const unsigned char*>(aProgID), progIdByteCount);
    if (ls != ERROR_SUCCESS)
        return false;

    DWORD hashByteCount = (::lstrlenW(hash.data()) + 1) * sizeof(wchar_t);
    ls = ::RegSetValueExW(userChoiceKey.get(), L"Hash", 0, REG_SZ, reinterpret_cast<const unsigned char*>(hash.data()), hashByteCount);
    if (ls != ERROR_SUCCESS)
        return false;

    return true;
}

static bool set_as_default_per_file_type(const std::wstring& extension, const std::wstring& prog_id)
{
    const std::wstring reg_extension = get_association_key_path(extension.c_str());
    if (reg_extension.empty())
        return false;

    bool needs_update = true;
    bool modified = false;
    HKEY rawAssocKey = nullptr;
    LSTATUS res = ::RegOpenKeyExW(HKEY_CURRENT_USER, reg_extension.c_str(), 0, KEY_READ, &rawAssocKey);
    AutoRegKey assoc_key(rawAssocKey);
    if (res == ERROR_SUCCESS) {
        DWORD data_size_bytes = 0;
        res = ::RegGetValueW(assoc_key.get(), L"UserChoice", L"ProgId", RRF_RT_REG_SZ, nullptr, nullptr, &data_size_bytes);
        if (res == ERROR_SUCCESS) {
            // +1 in case dataSizeBytes was odd, +1 to ensure termination
            DWORD data_size_chars = (data_size_bytes / sizeof(wchar_t)) + 2;
            std::wstring curr_prog_id(data_size_chars, L'\0');
            res = ::RegGetValueW(assoc_key.get(), L"UserChoice", L"ProgId", RRF_RT_REG_SZ, nullptr, curr_prog_id.data(), &data_size_bytes);
            if (res == ERROR_SUCCESS) {
                const std::wstring::size_type pos = curr_prog_id.find_first_of(L'\0');
                if (pos != std::wstring::npos)
                    curr_prog_id = curr_prog_id.substr(0, pos);
                needs_update = !boost::algorithm::iequals(curr_prog_id, prog_id);
            }
        }
    }

    if (needs_update)
        modified = set_user_choice(extension.c_str(), prog_id.c_str());

    return modified;
}

void associate_file_type(const std::wstring& extension, const std::wstring& prog_id, const std::wstring& prog_desc, bool set_as_default)
{
    assert(!extension.empty() && extension.front() == L'.');

    const std::wstring reg_extension       = L"SOFTWARE\\Classes\\" + extension;
    const std::wstring reg_prog_id         = L"SOFTWARE\\Classes\\" + prog_id;
    const std::wstring reg_prog_id_command = L"SOFTWARE\\Classes\\" + prog_id + +L"\\Shell\\Open\\Command";

    wchar_t app_path[1040];
    ::GetModuleFileNameW(nullptr, app_path, sizeof(app_path));
    const std::wstring prog_command = L"\"" + std::wstring(app_path) + L"\"" + L" \"%1\"";

    bool modified = false;
    modified |= set_into_win_registry(HKEY_CURRENT_USER, reg_extension.c_str(), prog_id.c_str());
    modified |= set_into_win_registry(HKEY_CURRENT_USER, reg_prog_id.c_str(), prog_desc.c_str());
    modified |= set_into_win_registry(HKEY_CURRENT_USER, reg_prog_id_command.c_str(), prog_command.c_str());
    if (set_as_default)
        modified |= set_as_default_per_file_type(extension, prog_id);

    // notify Windows only when any of the values gets changed
    if (modified)
        ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

} // namespace Slic3r

#endif // _WIN32
