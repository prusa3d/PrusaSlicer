#pragma once

#include "Slic3r/Domain/Types.hpp"

#include <string>

namespace Slic3r::App::Scene {

class Material;

// The following value must match MAX_LIGHTS defined into the shaders
static constexpr size_t MAX_NUM_LIGHTS = 4;

enum class LightReferenceSystem
{
    World,
    Camera,
    COUNT
};

static constexpr size_t LIGHT_REFERENCE_SYSTEMS_COUNT = size_t(LightReferenceSystem::COUNT);

std::string to_string(LightReferenceSystem sys);

static constexpr LightReferenceSystem DEFAULT_LIGHT_REFERENCE_SYSTEMS = LightReferenceSystem::World;
static const Domain::Vec3f DEFAULT_LIGHT_DIRECTION = { 0.0f, 0.0f, -1.0f };
static constexpr float DEFAULT_LIGHT_AMBIENT = 0.25f;
static constexpr float DEFAULT_LIGHT_DIFFUSE = 0.1f;
static constexpr float DEFAULT_LIGHT_SPECULAR = 0.1f;
static constexpr float DEFAULT_LIGHT_SHININESS = 1.0f;

struct Light
{
    LightReferenceSystem system{ DEFAULT_LIGHT_REFERENCE_SYSTEMS };
    Domain::Vec3f direction{ DEFAULT_LIGHT_DIRECTION };
    float ambient{ DEFAULT_LIGHT_AMBIENT };
    float diffuse{ DEFAULT_LIGHT_DIFFUSE };
    float specular{ DEFAULT_LIGHT_SPECULAR };
    float shininess{ DEFAULT_LIGHT_SHININESS };
    bool shadows{ false };
};

using Lights = std::vector<Light>;

static const Lights DEFAULT_LIGHTS = {
    { LightReferenceSystem::Camera, { 0.4574957f, -0.4574957f, -0.7624929f }, 0.45f, 0.48f, 0.075f, 20.0f, false },
    { LightReferenceSystem::Camera, { -0.70014f, -0.140028f, -0.70014f }, 0.0f, 0.18f, 0.0f, 0.01f, false }
};

struct Lighting
{
    float ambient_intensity{ 2.0f * DEFAULT_LIGHT_AMBIENT };
    Lights lights{ DEFAULT_LIGHTS };
};

} // namespace Slic3r::App::Scene
