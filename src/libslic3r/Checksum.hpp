///|/ Copyright (c) 2026 Leonardo Zenzen @LZZZZ
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Checksum_hpp_
#define slic3r_Checksum_hpp_

#include <cstdint>
#include <optional>
#include <string>

#include <boost/filesystem/path.hpp>

namespace Slic3r {
namespace checksum {

// CRC-32 of the whole file, read in binary mode.
// This is the ISO 3309 / ITU-T V.42 variant (polynomial 0x04C11DB7 reflected, initial value
// 0xFFFFFFFF, final XOR 0xFFFFFFFF) as implemented by zlib's crc32() and by RepRapFirmware,
// which uses the returned value to verify uploads made through rr_upload.
// Returns nullopt if the file cannot be opened or a read error occurs.
std::optional<uint32_t> crc32_file(const boost::filesystem::path &path);

// Lower case hexadecimal, zero padded to 8 digits, without the "0x" prefix.
std::string to_hex(uint32_t value);

} // namespace checksum
} // namespace Slic3r

#endif // slic3r_Checksum_hpp_
