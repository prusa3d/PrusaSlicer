#pragma once

#include "Slic3r/App/Render/Types.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/BedRef.hpp"

#include <vector>

namespace Slic3r::App::Plater {

struct BedThumbnailTextureRequest
{
    Domain::SelectionId project_id;
    Domain::BedRef bed_ref;
    Render::PixelFormat pixel_format{ Render::PixelFormat::RGBA8 };
    Render::Sizes sizes;

    bool operator == (const BedThumbnailTextureRequest& other) const {
        return project_id == other.project_id &&
            bed_ref == other.bed_ref &&
            pixel_format == other.pixel_format &&
            sizes == other.sizes;
    }
};

using BedThumbnailTextureRequests = std::vector<BedThumbnailTextureRequest>;

} // namespace Slic3r::App::Plater