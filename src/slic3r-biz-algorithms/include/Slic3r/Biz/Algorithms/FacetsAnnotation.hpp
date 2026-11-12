#pragma once

#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/FacetsAnnotation.hpp"
#include "Slic3r/Domain/TriangleSelector.hpp"

namespace Slic3r::Biz::Algorithms::FacetsAnnotation {

bool has_facets(const Domain::FacetsAnnotation &facets_annotation, Domain::TriangleSelector::TriangleStateType type);

indexed_triangle_set get_facets(const Domain::FacetsAnnotation &facets_annotation, const Domain::ModelVolume &model_volume, Domain::TriangleSelector::TriangleStateType type);
indexed_triangle_set get_facets_strict(const Domain::FacetsAnnotation &facets_annotation, const Domain::ModelVolume &model_volume, Domain::TriangleSelector::TriangleStateType type);

Domain::indexed_triangle_set_with_color get_all_facets_with_colors(const Domain::FacetsAnnotation &facets_annotation, const Domain::ModelVolume &model_volume);
Domain::indexed_triangle_set_with_color get_all_facets_strict_with_colors(const Domain::FacetsAnnotation &facets_annotation, const Domain::ModelVolume &model_volume);

} // namespace Slic3r::Biz::Algorithms::FacetsAnnotation
