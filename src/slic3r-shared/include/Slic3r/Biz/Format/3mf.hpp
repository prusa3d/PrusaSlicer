#pragma once

#include <string>
#include <vector>
#include "Slic3r/Biz/Format/Metadata.hpp"
#include "Slic3r/Biz/Format/ResultLoad3mf.hpp"
#include "Slic3r/Domain/Size.hpp"

namespace Slic3r {
namespace Domain {
class Model;
class Project;
class Image;
} // namespace Domain

// The following function may throw Loaded3MFException.
Loaded3MF load_3mf(const std::string& filepath_3mf);

struct Store3mfParam
{
    // Publish option to hide imported local file path
    bool fullpath_sources = true;

    // Preview for stored geometry
    // Used as file icon of the 3mf file by OPC
    // NOTE: In future it will be generated inside of store function
    const Domain::Image* thumbnail{nullptr};

    // Flag to force using of the zip64 compression function
    bool zip64 = true;

    // Use https://github.com/3MFConsortium/spec_production/releases/tag/1.2.0
    // to store huge model geometries into separated files (faster store/load)
    bool use_production_extension = false;

    // stored uncompressed 3mf - better versioning of uncompressed data
    // "0 - The file is stored (no compression)" in accordance with the OPC specification
    // ("Annex C, (normative) ZIP Appnote.txt Clarifications
    bool use_uncompressed_version = false;

    CT_Metadata_Model metadata;
};

void store_3mf(
    const std::string& filepath,
    const Domain::Project& project,
    const Store3mfParam& param = Store3mfParam{}
);

std::vector<Domain::Image> get_thumbnail_images_from_3mf(
    const std::string& input_file,
    const std::vector<Domain::Size>& sizes
);

} // namespace Slic3r
