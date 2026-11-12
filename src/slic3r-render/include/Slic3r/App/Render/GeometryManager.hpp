#pragma once

#include "Slic3r/App/Render/ResourceManager.hpp"
#include "Slic3r/App/Render/Geometry.hpp"

namespace Slic3r::App::Render {

template <typename K>
using GeometryManager = ResourceManager<K, Geometry>;

}
