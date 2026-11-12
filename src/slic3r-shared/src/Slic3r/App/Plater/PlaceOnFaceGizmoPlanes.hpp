#pragma once

#include "Slic3r/Domain/TriangleMesh.hpp"

namespace Slic3r::Domain { class ModelObject; }

namespace Slic3r::App::Plater {

struct PlaneData {
    Domain::Vec3f normal;
    indexed_triangle_set its;
};

/**
 * @brief Returns calculated planes for place on face and their normals.
 *
 * All data are in instance coordinate system.
 *
 * @param model_object The model object for which to calculate the planes.
 * @return A vector of PlaneData, each containing a normal and an indexed_triangle_set.
 */
std::vector<PlaneData> calculate_planes(const Domain::ModelObject& model_object);
}
