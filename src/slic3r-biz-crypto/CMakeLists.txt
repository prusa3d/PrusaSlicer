project(slic3r-biz-crypto)

set(biz_crypto_SOURCES
    include/Slic3r/Biz/Crypto/Hash.hpp
    src/Slic3r/Biz/Crypto/Hash.cpp
    include/Slic3r/Biz/Crypto/Sign.hpp
    include/Slic3r/Biz/Crypto/Types.hpp
    src/Slic3r/Biz/Crypto/Types.cpp
    src/Slic3r/Biz/Crypto/EvpHelper.hpp
    include/Slic3r/Biz/Crypto/Checksum.hpp
    src/Slic3r/Biz/Crypto/Checksum.cpp
    include/Slic3r/Biz/Crypto/ContentProvider.hpp
    src/Slic3r/Biz/Crypto/ContentProvider.cpp
    include/Slic3r/Biz/Crypto/SecureString.hpp
    src/Slic3r/Biz/Crypto/SecureString.cpp
)


if (WIN32)
    list(APPEND biz_crypto_SOURCES
        src/Slic3r/Biz/Crypto/Hash_win32.cpp
        src/Slic3r/Biz/Crypto/Sign_win32.cpp
    )
else ()
    list(APPEND biz_crypto_SOURCES
        src/Slic3r/Biz/Crypto/Hash_openssl.cpp
        src/Slic3r/Biz/Crypto/Sign_openssl.cpp
    )
endif ()

add_library(slic3r-biz-crypto STATIC ${biz_crypto_SOURCES})



target_include_directories(slic3r-biz-crypto PUBLIC include)
target_include_directories(slic3r-biz-crypto PRIVATE src)

target_link_libraries(slic3r-biz-crypto PUBLIC
    slic3r-base
    slic3r-biz-algorithms
    Boost::filesystem
)

if (WIN32)
    target_link_libraries(slic3r-biz-crypto PUBLIC
        bcrypt crypt32
    )
else ()
    find_package(OpenSSL REQUIRED)
    target_link_libraries(slic3r-biz-crypto PUBLIC
        OpenSSL::Crypto
    )
endif ()

if (SLIC3R_BUILD_TESTS)
    add_executable(slic3r-biz-crypto-tests
        test/Slic3r/Biz/Crypto/HashTests.cpp
        test/Slic3r/Biz/Crypto/SignTests.cpp
        test/Slic3r/Biz/Crypto/ChecksumTests.cpp
        test/Slic3r/TestData.hpp
    )
    target_include_directories(slic3r-biz-crypto-tests PRIVATE test)
    
    find_package(Catch2 3.8 REQUIRED)
    slic3r_remap_configs(Catch2::Catch2 RelWithDebInfo Release)
    slic3r_remap_configs(Catch2::Catch2WithMain RelWithDebInfo Release)
    
    target_link_libraries(slic3r-biz-crypto-tests PRIVATE slic3r-biz-crypto Catch2::Catch2WithMain)
    
    list(APPEND _catch_args "${CATCH_EXTRA_ARGS}")
    
    set(TEST_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test/data)
    file(TO_NATIVE_PATH "${TEST_DATA_DIR}" TEST_DATA_DIR)
    target_compile_definitions(slic3r-biz-crypto-tests PRIVATE TEST_DATA_DIR=R"\(${TEST_DATA_DIR}\)")
    
    add_test(NAME slic3r-biz-crypto-tests
        COMMAND slic3r-biz-crypto-tests ${_catch_args}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif ()

