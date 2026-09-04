#pragma once

#include <memory>
#include <string>
#include "Slic3r/Biz/Emboss/Emboss.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Domain/EmbossShape.hpp" // ExPolygonsWithIds

namespace Slic3r::Biz::Emboss {
/**
@brief Provide ability to lazy create shape for Embossing
Text have to load font file and create shapes for glyphs
SVG have to load SVG file and create shapes for paths
Different store into volume
*/
class ShapeProvider
{
public:
    explicit ShapeProvider(const Domain::EmbossShape& shape, TextLines text_lines = {}) :
        m_shape(shape), // copy
        m_text_lines(text_lines)
    {}

    virtual ~ShapeProvider() = default;

    /**
    @brief Write data how to reconstruct shape to volume
    @param volume Data object for store emboss params
    */
    virtual void write(Domain::ModelVolume& volume) const;

    /**
    @brief Used only with text for embossing per glyph
           \note Only for new volume creation(without ui)
    @param tr Embossed volume final transformation in world
    @param object Contain volumes to be sliced to text lines
    @return True on succes otherwise False(Per glyph shoud be disabled)
    */
    virtual void create_text_lines(const Domain::Transform3d& tr, const Domain::ModelObject& object)
    {}

    /**
    @brief Text extract glyphs from font file
    @param make_union Flag, when true union of expolygon is forced to calculate
    @return True on succes otherwise False
    */
    virtual bool create_shape()
    {
        return !m_shape.final_shape.expolygons.empty();
    }

    bool create_shape_with_union();

    const Domain::EmbossShape& get_shape() const
    {
        return m_shape;
    }

    const TextLines& get_text_lines() const
    {
        return m_text_lines;
    }

    const Domain::EmbossProjection& get_projection() const
    {
        return m_shape.projection;
    }

protected:
    Domain::EmbossShape m_shape;

    // Define per letter projection on one text line
    // [optional] It is not used when empty
    TextLines m_text_lines = {};
};

using ShapeProviderPtr = std::unique_ptr<ShapeProvider>;

} // namespace Slic3r::Biz::Emboss
