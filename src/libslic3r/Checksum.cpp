///|/ Copyright (c) 2026 Leonardo Zenzen @LZZZZ
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "Checksum.hpp"

#include <ios>
#include <vector>

#include <boost/crc.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

namespace Slic3r {
namespace checksum {

std::optional<uint32_t> crc32_file(const boost::filesystem::path &path)
{
    boost::nowide::ifstream file(path.string(), std::ios::in | std::ios::binary);
    if (! file.is_open()) {
        BOOST_LOG_TRIVIAL(error) << "Checksum: Could not open " << path << " to compute its CRC32";
        return std::nullopt;
    }

    boost::crc_32_type crc;
    // Heap allocated, this runs on the print host worker thread and the buffer is too big for its stack.
    std::vector<char> buffer(64 * 1024);
    for (;;) {
        file.read(buffer.data(), buffer.size());
        const std::streamsize read = file.gcount();
        if (read > 0)
            crc.process_bytes(buffer.data(), static_cast<size_t>(read));
        // A short read means we reached the end of the file. Note that this also sets failbit, so the
        // stream state below must be checked with bad() rather than fail().
        if (read < static_cast<std::streamsize>(buffer.size()))
            break;
    }

    if (file.bad()) {
        BOOST_LOG_TRIVIAL(error) << "Checksum: Error reading " << path << " to compute its CRC32";
        return std::nullopt;
    }

    return crc.checksum();
}

std::string to_hex(uint32_t value)
{
    static const char digits[] = "0123456789abcdef";

    std::string out(8, '0');
    for (int i = 7; i >= 0; -- i) {
        out[i] = digits[value & 0xF];
        value >>= 4;
    }
    return out;
}

} // namespace checksum
} // namespace Slic3r
