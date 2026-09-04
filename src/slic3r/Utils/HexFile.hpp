///|/ Copyright (c) Prusa Research 2018 - 2021 Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Hex_hpp_
#define slic3r_Hex_hpp_

#include <string>
#include <boost/filesystem/path.hpp>


namespace Slic3r {
namespace Utils {


struct HexFile
{
	enum DeviceKind {
		DEV_GENERIC,
		DEV_MK2,
		DEV_MK3,
		DEV_MM_CONTROL,
		DEV_CW1,
		DEV_CW1S,
	};

	boost::filesystem::path path;
	DeviceKind device = DEV_GENERIC;
	std::string model_id;

	HexFile() {}
	HexFile(boost::filesystem::path path);
};


}
}

#endif
