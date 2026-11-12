#pragma once

#include "Slic3r/Domain/PixelFormat.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "libslic3r/ThumbnailType.hpp"
#include "Slic3r/Domain/Size.hpp"

#include <vector>

namespace Slic3r::Biz::Slicing {

struct ThumbnailParams
{
    Domain::SelectionId project_id;
    Domain::SelectionId bed_instance_id;
    bool bed_instance_with_error;
    Domain::PixelFormat pixel_format{Domain::PixelFormat::RGBA8};
    Domain::Sizes sizes;

    bool operator==(const ThumbnailParams& other) const
    {
        return project_id == other.project_id
            && bed_instance_id == other.bed_instance_id
            && bed_instance_with_error == other.bed_instance_with_error
            && pixel_format == other.pixel_format
            && sizes == other.sizes;
    }
};

struct ThumbnailImageRequest
{
    ThumbnailType type;
    ThumbnailParams params;

    bool operator==(const ThumbnailImageRequest& other) const
    {
        return type == other.type && params == other.params;
    }
};

using ThumbnailImageRequests = std::vector<ThumbnailImageRequest>;

} // namespace Slic3r::Biz
