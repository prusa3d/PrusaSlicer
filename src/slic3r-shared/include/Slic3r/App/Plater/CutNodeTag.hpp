#pragma once
#include "GizmoNodeTag.hpp"
#include "Slic3r/Domain/CutConnector.hpp"

#include <boost/functional/hash.hpp>

#include <cstdint>
#include <optional>

namespace Slic3r::App::Plater {

/**
 * @brief Base tag structure for CutGizmo node types.
 *
 * This class serves as a common base for all CutGizmo node tags.
 * It provides a shared type hierarchy for nodes representing
 * different visual or interactive elements of the cutting gizmo.
 *
 * @note All node types used by CutGizmo inherit from this structure.
 */
struct CutNodeTag
{};

/**
 * @brief Node tag for CutGizmo used to represent the cutting plane
 */
struct CutPlaneNodeTag : public CutNodeTag
{
    explicit CutPlaneNodeTag() : CutNodeTag() {}
};

/**
 * @brief Node tag for CutGizmo used to represent the cutting line
 */
struct CutLineNodeTag : public CutNodeTag
{
    explicit CutLineNodeTag() : CutNodeTag() {}
};

/**
 * @brief Node tag for CutGizmo representing a part of the cut object.
 *
 * This tag identifies nodes that belong to a specific part of the
 * object being cut, such as the upper or lower section after a split.
 */
struct CutPartNodeTag : public CutNodeTag
{
    enum class Type
    {
        Undef = 0, ///< Undefined part.
        Upper, ///< Upper part of the object.
        Lower, ///< Lower part of the object.
    };

    Type type{Type::Undef}; ///< Type of the part.
    const size_t id; ///< Unique identifier for the part.

    explicit CutPartNodeTag(Type type, size_t id) : CutNodeTag(), type(type), id(id) {}
};

/**
 * @brief Node tag for CutGizmo representing a mesh that connects cut parts.
 *
 * This tag identifies nodes that correspond to a concrete mesh used
 * to physically connect two parts of the cut object.
 */
struct CutConnectorNodeTag : public CutNodeTag
{
    size_t id; ///< Unique identifier for the connector mesh.
    bool is_selected{true};
    bool is_snap{false};

    explicit CutConnectorNodeTag(size_t id, bool is_snap = false) :
        CutNodeTag(),
        id(id),
        is_snap(is_snap)
    {}

    // used for connectors root node
    explicit CutConnectorNodeTag() : CutNodeTag(), id(size_t(-1)) {}
};

/**
 * @brief Represents an interactive handle in the CutGizmo.
 *
 * Handles can be used to move or rotate the cut plane along a specific axis.
 */
struct Handle
{
    enum class Type
    {
        Undef,
        Move,
        Rotation
    };

    Type type{Type::Undef};
    AxisType axis{AxisType::None};

    // Equality operator
    bool operator==(const Handle& other) const
    {
        return type == other.type && axis == other.axis;
    }

    bool is_undef() const
    {
        return type == Type::Undef && axis == AxisType::None;
    }

    // Type queries
    bool is_move() const
    {
        return type == Type::Move;
    }

    bool is_rotation() const
    {
        return type == Type::Rotation;
    }

    // Axis queries
    bool is_x_axis() const
    {
        return axis == AxisType::XAxis;
    }

    bool is_y_axis() const
    {
        return axis == AxisType::YAxis;
    }

    bool is_z_axis() const
    {
        return axis == AxisType::ZAxis;
    }

    // Combined type + axis queries (optional convenience)
    bool is_move_x() const
    {
        return is_move() && is_x_axis();
    }

    bool is_move_z() const
    {
        return is_move() && is_z_axis();
    }

    bool is_rotation_x() const
    {
        return is_rotation() && is_x_axis();
    }

    bool is_rotation_y() const
    {
        return is_rotation() && is_y_axis();
    }

    bool is_rotation_z() const
    {
        return is_rotation() && is_z_axis();
    }
};

/**
 * @brief Node tag for CutGizmo used to represent interactive transformation handles.
 *
 * This tag identifies nodes that belong to the transformation handles (move or rotate)
 * of the CutGizmo. Each handle is associated with a primary axis and an optional
 * orientation (CW or CCW for rotation).
 */
struct CutHandleNodeTag : public CutNodeTag
{
    enum class Type
    {
        Undef, ///< Undefined type.
        Handle, ///< Represents the main interactive handle.
        GradedCircle, ///< Used by rotation handles to visualize rotation angles.
        Stem, ///< Used by both move and rotation handles as a connector line.
    };

    AxisType primary_axis{AxisType::None};
    Type type{Type::Undef};
    Handle::Type handle_type{Handle::Type::Undef};
    std::optional<bool> is_cw;

    explicit CutHandleNodeTag() {}

    explicit CutHandleNodeTag(
        Type type,
        Handle::Type handle_type,
        AxisType primary_axis     = AxisType::None,
        std::optional<bool> is_cw = std::nullopt
    ) :
        primary_axis(primary_axis),
        type(type),
        handle_type(handle_type),
        is_cw(is_cw)
    {}

    Handle handle() const
    {
        return Handle(handle_type, primary_axis);
    }
};

/**
 * @brief Struct used for tag of nodes for for Cut visual elements.
 *
 * It identifies nodes that belong to the cut parts and cut plane.
 */
struct CutAuxiliaryElementId
{
    enum class Type : uint8_t
    {
        Undef = 0,
        CutPlane,
        CutPart,
    };

    Type type;
    size_t id;

    /**
     *
     * @param rhs
     * @return
     */
    bool operator==(const CutAuxiliaryElementId& rhs) const
    {
        return type == rhs.type && id == rhs.id;
    }

    bool operator<(const CutAuxiliaryElementId& rhs) const
    {
        return type < rhs.type || (type == rhs.type && id < rhs.id);
    }
};

/**
 * @brief Struct used for tag of nodes for for CutConnectors elements.
 *
 * It identifies nodes that belong to the cut parts and cut plane.
 */
struct ConnectorAuxiliaryElementId
{
    Domain::CutConnectorAttributes attributes;

    /**
     *
     * @param rhs
     * @return
     */
    bool operator==(const ConnectorAuxiliaryElementId& rhs) const
    {
        return attributes == rhs.attributes;
    }

    bool operator<(const ConnectorAuxiliaryElementId& rhs) const
    {
        return attributes < rhs.attributes;
    }
};

} // namespace Slic3r::App::Plater

namespace std {
template <>
struct hash<Slic3r::App::Plater::CutAuxiliaryElementId>
{
    using value_type = Slic3r::App::Plater::CutAuxiliaryElementId;

    std::uint64_t operator()(const value_type& val) const
    {
        size_t ret = boost::hash_value(val.type);
        boost::hash_combine(ret, val.id);
        return ret;
    }
};

template <>
struct hash<Slic3r::App::Plater::ConnectorAuxiliaryElementId>
{
    using value_type = Slic3r::App::Plater::ConnectorAuxiliaryElementId;

    std::uint64_t operator()(const value_type& val) const
    {
        size_t ret = boost::hash_value(val.attributes.type);
        boost::hash_combine(ret, val.attributes.shape);
        boost::hash_combine(ret, val.attributes.style);
        return ret;
    }
};
} // namespace std
