// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#if _WIN32_WINNT < 0x0A00
#error The native architecture check requires Windows 10 or newer
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstddef>
#include <gmp.h>
#include <mpfr.h>
#include <windows.h>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "Failed: %s (line %d)\n", #condition, __LINE__); \
    return 1; } } while (false)

static DWORD WINAPI check_thread_precision(void*)
{
    mpfr_set_default_prec(80);
    mpfr_t value;
    mpfr_init(value);
    const bool correct = mpfr_get_prec(value) == 80;
    mpfr_clear(value);
    mpfr_free_cache();
    return correct ? 0 : 1;
}

int main()
{
    static_assert(sizeof(void*) == 8, "Only 64-bit targets are supported");
    static_assert(sizeof(long) == 4, "Windows LLP64 is required");
    static_assert(sizeof(long double) == 8, "MSVC long double ABI is required");
    static_assert(sizeof(mp_limb_t) == 8 && GMP_NUMB_BITS == 64 && GMP_NAIL_BITS == 0,
                  "The package must use 64-bit limbs without nails");
    static_assert(sizeof(mp_bitcnt_t) == 4 && sizeof(mp_size_t) == 4,
                  "GMP scalar types must retain the Windows LLP64 layout");
    static_assert(sizeof(mp_limb_t) * 8 == GMP_LIMB_BITS, "GMP limb mismatch");
    USHORT process_machine = 0, native_machine = 0;
    CHECK(IsWow64Process2(GetCurrentProcess(), &process_machine, &native_machine));
    CHECK(process_machine == IMAGE_FILE_MACHINE_UNKNOWN);
#if defined(_M_ARM64) || defined(__aarch64__)
    CHECK(native_machine == IMAGE_FILE_MACHINE_ARM64);
#elif defined(_M_X64) || defined(__x86_64__)
    CHECK(native_machine == IMAGE_FILE_MACHINE_AMD64);
#else
#error Unsupported architecture
#endif
    char executable[MAX_PATH], library[MAX_PATH];
    DWORD length = GetModuleFileNameA(nullptr, executable, MAX_PATH);
    CHECK(length > 0 && length < MAX_PATH);
    char* separator = std::strrchr(executable, '\\');
    CHECK(separator != nullptr);
    separator[1] = '\0';
    const char* names[] = {"libgmp-10.dll", "libmpfr-6.dll"};
    for (const char* name : names) {
        HMODULE module = GetModuleHandleA(name);
        CHECK(module != nullptr);
        length = GetModuleFileNameA(module, library, MAX_PATH);
        CHECK(length > 0 && length < MAX_PATH);
        separator = std::strrchr(library, '\\');
        CHECK(separator != nullptr);
        separator[1] = '\0';
        CHECK(_stricmp(executable, library) == 0);
    }
    CHECK(std::strcmp(gmp_version, "6.2.1") == 0);
    CHECK(std::strcmp(mpfr_get_version(), "4.2.1") == 0);
    CHECK(mp_bits_per_limb == GMP_LIMB_BITS);
    CHECK(mpfr_buildopt_tls_p() != 0);
    mpfr_set_default_prec(113);
    HANDLE thread = CreateThread(nullptr, 0, check_thread_precision, nullptr, 0, nullptr);
    CHECK(thread != nullptr);
    CHECK(WaitForSingleObject(thread, 30000) == WAIT_OBJECT_0);
    DWORD thread_result = 1;
    CHECK(GetExitCodeThread(thread, &thread_result));
    CHECK(CloseHandle(thread));
    CHECK(thread_result == 0);
    CHECK(mpfr_get_default_prec() == 113);

    mpz_t value, expected;
    mpz_inits(value, expected, nullptr);
    mpz_ui_pow_ui(value, 2, 100);
    CHECK(mpz_set_str(expected, "1267650600228229401496703205376", 10) == 0);
    CHECK(mpz_cmp(value, expected) == 0);
    mpz_add_ui(value, value, 123);
    CHECK(mpz_get_ui(value) == 123);
    CHECK(mpz_fdiv_ui(value, 1000) == 499);
    char* text = mpz_get_str(nullptr, 10, value);
    CHECK(std::strcmp(text, "1267650600228229401496703205499") == 0);
    void (*gmp_free)(void*, size_t) = nullptr;
    mp_get_memory_functions(nullptr, nullptr, &gmp_free);
    gmp_free(text, std::strlen(text) + 1);

    mp_limb_t limbs[] = {0, 1};
    mp_limb_t negated[2] = {};
    CHECK(mpn_neg(negated, limbs, 2) == 1);
    CHECK(negated[0] == 0 && negated[1] == GMP_NUMB_MAX);

    mpq_t rational, increment;
    mpq_inits(rational, increment, nullptr);
    mpq_set_ui(rational, 1, 3);
    mpq_set_ui(increment, 1, 6);
    mpq_add(rational, rational, increment);
    CHECK(mpq_cmp_ui(rational, 1, 2) == 0);

    mpfr_t real, square;
    mpfr_inits2(256, real, square, (mpfr_ptr) nullptr);
    CHECK(mpfr_set_q(real, rational, MPFR_RNDN) == 0);
    CHECK(mpfr_cmp_d(real, 0.5) == 0);
    CHECK(mpfr_set_ld(real, 0.125L, MPFR_RNDN) == 0);
    CHECK(mpfr_get_ld(real, MPFR_RNDN) == 0.125L);
    mpfr_set_ui(real, 2, MPFR_RNDN);
    mpfr_sqrt(real, real, MPFR_RNDN);
    mpfr_mul(square, real, real, MPFR_RNDN);
    mpfr_sub_ui(square, square, 2, MPFR_RNDN);
    mpfr_abs(square, square, MPFR_RNDN);
    CHECK(mpfr_cmp_ui_2exp(square, 1, -250) < 0);
    mpfr_exp_t exponent = 0;
    char* digits = mpfr_get_str(nullptr, &exponent, 10, 20, real, MPFR_RNDN);
    CHECK(exponent == 1);
    CHECK(std::strcmp(digits, "14142135623730950488") == 0);
    mpfr_free_str(digits);
    mpfr_clears(real, square, (mpfr_ptr) nullptr);
    mpfr_free_cache();
    mpq_clears(rational, increment, nullptr);
    mpz_clears(value, expected, nullptr);

    // Compare this exact output between the library and consumer compilers.
    std::printf("native=%04x pointer=%zu long=%zu limb=%zu mpz=%zu mpz_d=%zu "
                "mpq=%zu mpfr=%zu prec=%zu exp=%zu mpfr_d=%zu gmp=%s mpfr=%s\n",
                native_machine, sizeof(void*), sizeof(long), sizeof(mp_limb_t),
                sizeof(__mpz_struct), offsetof(__mpz_struct, _mp_d),
                sizeof(__mpq_struct), sizeof(__mpfr_struct), sizeof(mpfr_prec_t),
                sizeof(mpfr_exp_t), offsetof(__mpfr_struct, _mpfr_d),
                gmp_version, mpfr_get_version());
    return 0;
}
