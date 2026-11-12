#pragma once

#include "Slic3r/App/Render/CommandBuffer.hpp"
#include "Slic3r/App/Scene/IRenderLayerObject.hpp"
#include "Slic3r/App/Scene/Camera.hpp"
#include "Slic3r/App/Render/Material.hpp"
#include "Slic3r/App/Render/Types.hpp"
#include "Slic3r/App/Scene/Lights.hpp"
#include "Slic3r/App/Scene/LightingHelper.hpp"
#include "Slic3r/App/Scene/PrintVolumeData.hpp"

#include <optional>

namespace Slic3r::App::Scene {

class Node;

/**
 * @brief Generic 3D object rendering interface
 */
class IRenderNodeComponent : public IRenderLayerObject {
public:
    /**
     * Render 3D object associated with node
     * @param node Node the render component belongs to (and uses its world transform)
     * @param camera Scene camera defining view and projection matrices.
     * @param material_override Potential material override coming for the node or its parents.
     * @param cmd_buffer Command buffer the render commands are passed to.
     */
    virtual void render(const Node& node, const Camera& camera, const Render::Material& material_override,
        Render::CommandBuffer& cmd_buffer) const = 0;

    /**
     * @brief Get associated material.
     * @return
     */
    virtual const Render::Material& material() const = 0;

    /**
     * @brief replace the associated material with the given one.
     */
    virtual void replace_material(const Render::Material& material) = 0;

    /**
     * @brief Set associated shadows data.
     */
    virtual void set_shadows(const Render::Shadows& shadows) = 0;

    virtual bool cast_shadows() const = 0;
    virtual bool receive_shadows() const = 0;

    /**
     * @brief Set associated pbr data.
     */
    virtual void set_pbr(const PBRParams& pbr) = 0;

    virtual bool has_pbr() const = 0;
    virtual const std::optional<PBRParams>& pbr() const = 0;

    /**
     * @brief Set associated print volume data.
     */
    virtual void set_print_volume(const PrintVolumeData& print_volume) = 0;

    virtual bool has_print_volume() const = 0;
    virtual const std::optional<PrintVolumeData>& print_volume() const = 0;
};

} // namespace Slic3r::App::Scene

