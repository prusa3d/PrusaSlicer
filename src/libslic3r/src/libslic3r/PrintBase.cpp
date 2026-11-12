#include "Slic3r/Exception.hpp"
#include "libslic3r/PrintBase.hpp"
#include "Slic3r/Biz/Algorithms/Model.hpp"
#include "Slic3r/Domain/Axis.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"

#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

#include "Slic3r/Math.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/I18N_private.hpp"
#include "Slic3r/Version.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/PolygonUtils.hpp"

using namespace Slic3r::Biz;

namespace Slic3r
{

using Slic3r::Biz::Parser::PlaceholderParser;
using ParserConfig = Slic3r::Biz::Parser::IO::Config;
using Value = Slic3r::Biz::Parser::IO::Value;
using Scalar = Slic3r::Biz::Parser::IO::Scalar;
using Domain::ConfigPack;
using Domain::ConfigPackFDM;


void PrintTryCancel::operator()() const
{
    m_print->throw_if_canceled();
}

// Update "scale", "input_filename", "input_filename_base" placeholders from the current m_objects.
ParserConfig PrintBase::get_object_placeholders() const
{
    ParserConfig config;
    // get the first input file name
    std::string input_file;
    std::vector<std::string> v_scale;
    int num_objects = 0;
    int num_instances = 0;
	for (const Domain::ModelObject *model_object : m_model.objects) {
        Domain::ModelInstance *printable = nullptr;
		for (Domain::ModelInstance *model_instance : model_object->instances) {
            printable = model_instance;
            ++ num_instances;
        }
		if (printable) {
            ++ num_objects;
	        // CHECK_ME -> Is the following correct ?
			v_scale.push_back("x:" + boost::lexical_cast<std::string>(printable->get_scaling_factor(Domain::X) * 100) +
				"% y:" + boost::lexical_cast<std::string>(printable->get_scaling_factor(Domain::Y) * 100) +
				"% z:" + boost::lexical_cast<std::string>(printable->get_scaling_factor(Domain::Z) * 100) + "%");
	        if (input_file.empty())
	            input_file = model_object->name.empty() ? model_object->input_file : model_object->name;
	    }
    }

    config.set("num_objects", num_objects);
    config.set("num_instances", num_instances);

    config.set("scale", v_scale);
    if (! input_file.empty()) {
        // get basename with and without suffix
        const std::string input_filename = boost::filesystem::path(input_file).filename().string();
        const std::string input_filename_base = input_filename.substr(0, input_filename.find_last_of("."));
//        config.set_key_value("input_filename", new ConfigOptionString(input_filename_base + default_output_ext));
        config.set("input_filename_base", input_filename_base);
    }

    return config;
}

// Generate an output file name based on the format template, default extension, and template parameters
// (timestamps, object placeholders derived from the model, current placeholder prameters, print statistics - config_override)
std::string PrintBase::output_filename(const std::string &format, const std::string &default_ext, const std::string &filename_base, const Biz::Parser::IO::Config *config_override) const
{
    ParserConfig cfg;
    if (config_override != nullptr)
    	cfg = *config_override;
    cfg.set("version", std::string{SLIC3R_VERSION});
    PlaceholderParser::update_timestamp(cfg);
    cfg.apply(this->get_object_placeholders());
    if (! filename_base.empty()) {
//		cfg.set_key_value("input_filename", new ConfigOptionString(filename_base + default_ext));
		cfg.set("input_filename_base", filename_base);
    }
    try {
        const Value* option{cfg.option("input_filename_base")};
        ASSERT(option != nullptr && is_scalar(*option));
		boost::filesystem::path filename = format.empty() ?
			std::get<Scalar>(*option).get<std::string>() + default_ext :
			this->placeholder_parser().process(format, 0, &cfg);
        if (filename.extension().empty())
            filename.replace_extension(default_ext);
        return filename.string();
    } catch (std::runtime_error &err) {
        throw Slic3r::PlaceholderParserError(_u8L("Failed processing of the output_filename_format template.") + "\n" + err.what());
    }
}

std::mutex& PrintObjectBase::state_mutex(PrintBase *print)
{
	return print->state_mutex();
}

static Domain::Polygon get_rectangle(double width, double height)
{
    return Domain::Polygon{
        scaled(Domain::Vec2d{-width / 2.0, -height / 2.0}),
        scaled(Domain::Vec2d{width / 2.0, -height / 2.0}),
        scaled(Domain::Vec2d{width / 2.0, height / 2.0}),
        scaled(Domain::Vec2d{-width / 2.0, height / 2.0})
    };
}

double Slicing::WipeTowerGeometry::get_height() const
{
    return depths.empty() ? fallback_height : depths.back().z;
}

Domain::ExPolygon Slicing::WipeTowerGeometry::get_outline(
    const Domain::ModelWipeTower& model_wipe_tower
) const
{
    const double depth{depths.empty() ? fallback_depth : depths.front().depth};

    const Domain::Polygon recangle{get_rectangle(width + 2 * brim_width, depth + 2 * brim_width)};
    const Domain::Polygon ellipse{Algorithms::PolygonUtils::create_ellipse(
        scaled<double>(cone_radius / cone_x_scale + brim_width),
        scaled<double>(cone_radius + brim_width),
        200
    )};

    Domain::ExPolygons outline{union_ex({recangle, ellipse})};
    ASSERT(outline.size() == 1);

    outline.front().translate(scaled(Vec2d{width / 2.0, depth / 2.0}));
    outline.front().rotate(Slic3r::deg2rad(model_wipe_tower.rotation));

    return outline.front();
}

Domain::BoundingBox3d Slicing::WipeTowerGeometry::get_bounding_box(
    const Domain::ModelWipeTower& model_wipe_tower
) const
{
    using Biz::Algorithms::BoundingBox::construct;
    using Biz::Algorithms::BoundingBox::merge;
    using Biz::Algorithms::BoundingBox::translated;
    using Biz::Algorithms::BoundingBox::unscaled;

    const ExPolygon polygon{get_outline(model_wipe_tower)};
    const Domain::BoundingBox2crd bb_2d_scaled{construct(polygon.contour.points)};
    const Domain::BoundingBox2d bb_2d{unscaled<double>(bb_2d_scaled)};

    const double height{get_height()};

    const Domain::BoundingBox3d bb_3d{
        {bb_2d.min.x(), bb_2d.min.y(), 0.0},
        {bb_2d.max.x(), bb_2d.max.y(), height}
    };
    return translated(
        bb_3d,
        Domain::Vec3d{model_wipe_tower.position.x(), model_wipe_tower.position.y(), 0.0}
    );
}

Domain::Vec2d Slicing::WipeTowerGeometry::get_center(
    const Domain::ModelWipeTower& model_wipe_tower
) const
{
    const Vec2d position{model_wipe_tower.position};

    const double depth{depths.empty() ? fallback_depth : depths.front().depth};
    Vec2d center{position + Vec2d{width / 2.0, depth / 2.0}};
    Eigen::Rotation2Dd rotation{Slic3r::deg2rad(model_wipe_tower.rotation)};
    center = rotation * (center - position) + position;
    return center;
}

} // namespace Slic3r
