#pragma once

#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/Model.hpp"

namespace Slic3r::Biz::Algorithms::Model {

Domain::ModelObject* add_object(Domain::Model* model, const char* name, const char* path, Domain::TriangleMesh&& mesh);

/**
 * Returns approximate axis aligned bounding box of this model.
 */
Domain::BoundingBox3d bounding_box_approx(const Domain::Model& model);

/**
 * Returns exact axis aligned bounding box of this model.
 */
Domain::BoundingBox3d bounding_box_exact(const Domain::Model& model);

/**
 * Propose an output file name & path based on the first printable object's name and source input file's path.
 */
std::string propose_export_file_name_and_path(const Domain::Model& model);

/**
 * Propose an output path, replace extension. The new_extension shall contain the initial dot.
 */
std::string propose_export_file_name_and_path(const Domain::Model& model, const std::string& new_extension);

/**
 * Returns true if any ModelObject was modified.
 */
bool center_instances_around_point(Domain::Model& model, const Domain::Vec2d& point);

Domain::TriangleMesh flatten_to_mesh(const Domain::Model& model);

void print_info(const Domain::Model& model);

} // namespace Slic3r::Biz::Algorithms::Model
