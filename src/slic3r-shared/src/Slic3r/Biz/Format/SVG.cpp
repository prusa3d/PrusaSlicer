#include <nanosvg/nanosvg.h>
#include <memory>
#include <string>
#include <utility>
#include <cassert>

#include "Slic3r/Biz/Format/SVG.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/EmbossShape.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include <Slic3r/Biz/I18N/I18N.hpp> // translations
#include "Slic3r/Biz/Emboss/Emboss.hpp"
#include "Slic3r/Biz/Emboss/EmbossJob.hpp"
#include "Slic3r/Biz/Emboss/NSVGUtils.hpp"
#include "admesh/stl.h"
#include "fmt/format.h"

using Slic3r::Domain::EmbossShape;
using Slic3r::Domain::ExPolygonsWithIds;
using Slic3r::Domain::EmbossProjection;

using namespace Slic3r::Biz;

namespace {
std::string get_file_name(const std::string &file_path)
{
    if (file_path.empty())
        return file_path;

    size_t pos_last_delimiter = file_path.find_last_of("/\\");
    if (pos_last_delimiter == std::string::npos) {
        // should not happend that in path is not delimiter
        assert(false);
        pos_last_delimiter = 0;
    }

    size_t pos_point = file_path.find_last_of('.');
    if (pos_point == std::string::npos || pos_point < pos_last_delimiter // last point is inside of directory path
    ) {
        // there is no extension
        assert(false);
        pos_point = file_path.size();
    }

    size_t offset = pos_last_delimiter + 1;             // result should not contain last delimiter ( +1 )
    size_t count  = pos_point - pos_last_delimiter - 1; // result should not contain extension point ( -1 )
    return file_path.substr(offset, count);
}
}

namespace Slic3r::Biz {
tl::expected<Domain::Model, std::string> load_svg(const std::string &input_file) {    
    Domain::EmbossShape shape{
        .projection = Domain::EmbossProjection{ .depth = 10./* mm */, .use_surface = false},
        .svg_file = std::make_optional<Domain::EmbossShape::SvgFile>(input_file) 
    };

    std::optional<double> no_scale;
    Emboss::ReadShapeResult res = Emboss::read_shape_from_file(shape, no_scale, no_scale);
    if (res != Emboss::ReadShapeResult::success)
        return tl::unexpected(Emboss::to_string(res, input_file));
    ASSERT(!shape.final_shape.expolygons.empty());

    // convert 2d shape to 3d triangles
    Emboss::ProjectTransform project = Biz::Emboss::create_projection(shape, true);
    indexed_triangle_set its = Emboss::polygons2model(shape.final_shape.expolygons, project);
    ASSERT(!its.empty());

    // add mesh to model
    Domain::Model output_model;
    Domain::ModelObject *object = output_model.add_object();
    ASSERT(object != nullptr);
    object->name = get_file_name(input_file);

    Domain::ModelVolume* volume = Algorithms::ModelObject::add_volume(
        object, Algorithms::TriangleMesh::construct(std::move(its)));
    ASSERT(volume != nullptr);

    volume->name = object->name; // copy
    volume->emboss_shape = std::move(shape);
    object->invalidate_bounding_box();
    return std::move(output_model);
}

}; // namespace Slic3r
