#include "PrintRequest.hpp"

#include <boost/property_tree/xml_parser.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/log/trivial.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem.hpp>
#include <fast_float.h>

#include "libslic3r/Exception.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Format/STL.hpp"

//#include "slic3r/GUI/format.hpp"

namespace Slic3r {
namespace pt = boost::property_tree;
namespace {
void read_file(const char* input_file, pt::ptree& tree)
{
	boost::filesystem::path path(input_file);
	
	boost::nowide::ifstream ifs(path.string());
	try {
		pt::read_xml(ifs, tree);
	}
	catch (const boost::property_tree::xml_parser::xml_parser_error&) {
		throw Slic3r::RuntimeError("Failed reading PrintRequest file. File format is corrupt.");
	}
}

void read_tree(const boost::property_tree::ptree::value_type& section, boost::filesystem::path& model_path, std::string& material, std::string& material_color, std::vector<std::string>& transformation_matrix)
{
	for (const auto& data : section.second) {
		if (data.first == "Path") {
			model_path = boost::filesystem::path(data.second.data());
		}
		else  if (data.first == "Material") {
			material = data.second.data();
		}
		else  if (data.first == "MaterialColor") {
			material_color = data.second.data();
		}
		else  if (data.first == "TransformationMatrix") {
			transformation_matrix.reserve(16);
			for (const auto& element : data.second) {
				transformation_matrix.emplace_back(element.second.data());
			}
		}
	}
}
bool fill_model(Model* model, const boost::filesystem::path& model_path, const std::string& material, const std::vector<std::string>& transformation_matrix)
{
	if (!boost::filesystem::exists(model_path))
		throw Slic3r::RuntimeError("Failed reading PrintRequest file. Path doesn't exists. " + model_path.string());
	if (!boost::algorithm::iends_with(model_path.string(), ".stl"))
		throw Slic3r::RuntimeError("Failed reading PrintRequest file. Path is not stl file. " + model_path.string());
	bool result = load_stl(model_path.string().c_str(), model);
	if (!material.empty()) {
		model->objects.back()->volumes.front()->set_material_id(material);
	}
	return result;
}
void add_instance(Model* model, const boost::filesystem::path& model_path, const std::vector<std::string>& transformation_matrix)
{
	if (transformation_matrix.size() >= 16) {

		auto string_to_double = [model_path](const std::string& from) -> double {
			double ret_val;
			auto answer = fast_float::from_chars(from.data(), from.data() + from.size(), ret_val);
			if (answer.ec != std::errc())
				throw Slic3r::RuntimeError("Failed reading PrintRequest file. Couldn't parse transformation matrix. " + model_path.string());
			return ret_val;
		};

		Vec3d offset_vector;
		Slic3r::Transform3d matrix;
		try
		{
			offset_vector = Slic3r::Vec3d(string_to_double(transformation_matrix[3]), string_to_double(transformation_matrix[7]), string_to_double(transformation_matrix[11]));
			// PrintRequest is row-major 4x4, Slic3r::Transform3d (Eigen) is column-major by default 3x3
			matrix(0, 0) = string_to_double(transformation_matrix[0]);
			matrix(1, 0) = string_to_double(transformation_matrix[1]);
			matrix(2, 0) = string_to_double(transformation_matrix[2]);
			matrix(0, 1) = string_to_double(transformation_matrix[4]);
			matrix(1, 1) = string_to_double(transformation_matrix[5]);
			matrix(2, 1) = string_to_double(transformation_matrix[6]);
			matrix(0, 2) = string_to_double(transformation_matrix[8]);
			matrix(1, 2) = string_to_double(transformation_matrix[9]);
			matrix(2, 2) = string_to_double(transformation_matrix[10]);
		}
		catch (const Slic3r::RuntimeError& e) {
			throw e;
		}


		ModelObject* object = model->objects.back();
		Slic3r::Geometry::Transformation transformation(matrix);
		transformation.set_offset(offset_vector);
		object->add_instance(transformation);
	}
}

}

bool load_printRequest(const char* input_file, Model* model)
{
	pt::ptree tree;
	try
	{
		read_file(input_file, tree);
	}
	catch (const std::exception& e)
	{
		throw e;
	}
	
	bool result = true;

	for (const auto& section0 : tree) {
		if (section0.first != "PrintRequest") 
			continue;
		if (section0.second.empty())
			continue;
		for (const auto& section1 : section0.second) {
			if (section1.first != "Files") 
				continue;
			if (section1.second.empty())
				continue;
			for (const auto& section2 : section1.second) {
				if (section2.first != "File") 
					continue;
				if (section2.second.empty())
					continue;
				boost::filesystem::path model_path;
				std::string material;
				std::string material_color;
				std::vector<std::string> transformation_matrix;
				
				try
				{
					read_tree(section2, model_path, material, material_color, transformation_matrix);
					result = result && fill_model(model, model_path, material, transformation_matrix);
					if (!result)
						return false;
					add_instance(model, model_path, transformation_matrix);
				}
				catch (const std::exception& e)
				{
					throw e;
				}
				
				
			}
		}
	}

	return true;
}

} // namespace Slic3r
