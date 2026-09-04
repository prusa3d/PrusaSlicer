#pragma once

#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/Image.hpp"
#include "libslic3r/ThumbnailType.hpp"

#include <vector>

namespace Slic3r::Biz::Slicing {

struct ThumbnailImageResult
{
    ThumbnailType type;
    Domain::SelectionId project_id;
    Domain::SelectionId bed_instance_id;
    Domain::Images images;
};

using ThumbnailImageResults = std::vector<ThumbnailImageResult>;

} // namespace Slic3r::Biz
