#pragma once

#include "Slic3r/App/Render/Texture.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

namespace Slic3r::App::Plater {

struct BedThumbnailTexture
{
    Domain::SelectionId project_id;
    Domain::SelectionId bed_instance_id;
    Render::TexturePtr thumbnail;
};

using BedThumbnailTextures = std::vector<BedThumbnailTexture>;

} // namespace Slic3r::App::Plater