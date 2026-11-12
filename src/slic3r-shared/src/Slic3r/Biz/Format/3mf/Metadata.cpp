#include "Slic3r/Biz/Format/Metadata.hpp"
#include "Slic3r/Biz/Utils/XmlEscape.hpp"

#include <string_view>

#include <boost/assign.hpp>
#include <boost/bimap.hpp>

namespace{
using namespace Slic3r;

using ModelMetadataToString = boost::bimap<ModelMetadataNames, std::string_view>;
const ModelMetadataToString type_to_name =
    boost::assign::list_of<ModelMetadataToString::relation>
    (ModelMetadataNames::Title           , "Title")
    (ModelMetadataNames::Designer        , "Designer")
    (ModelMetadataNames::Description     , "Description")
    (ModelMetadataNames::Copyright       , "Copyright")
    (ModelMetadataNames::LicenseTerms    , "LicenseTerms")
    (ModelMetadataNames::Rating          , "Rating")
    (ModelMetadataNames::CreationDate    , "CreationDate")
    (ModelMetadataNames::ModificationDate, "ModificationDate")
    (ModelMetadataNames::Application     , "Application")
    (ModelMetadataNames::Slic3r_version  , "slic3rpe:Version3mf");

void write_metadata(std::ostream &stream, const ModelMetadata &metadata) {
    stream << "<metadata name=\"" << Slic3r::Biz::Utils::xml_escape(std::string(to_name(metadata.name))) << "\"";
    // Not supported by xs
    //if (metadata.preserve)
    //    stream << " preserve=\"true\"";
    if (!metadata.type.empty())
        stream << " type=\"" << Slic3r::Biz::Utils::xml_escape(metadata.type) << "\"";
    stream << ">" 
        << Slic3r::Biz::Utils::xml_escape(metadata.value) << "</metadata>\n";
}

#ifndef NDEBUG // function is used only in assert
bool is_valid(const CT_Metadata_Model &metadata) {
    // Check that metadata has unique names
    std::set<std::string_view> used;
    for (const ModelMetadata &m : metadata)
        if (!used.insert(to_name(m.name)).second)
            return false;

    // TODO: check is value xml_escape() ?
    //std::string name = xml_escape(boost::filesystem::path(filename).stem().string());
    //stream << " <" << METADATA_TAG << " name=\"Title\">" << name << "</" << METADATA_TAG << ">\n";

    // Check that contain Version
    //if (used.find(to_name(ModelMetadataNames::Slic3r_version)) == used.end())
    //    return false; 

    return true;
}
#endif

} // namespace

namespace Slic3r{

std::string_view to_name(const ModelMetadata::NameType &name) {
    return std::holds_alternative<std::string>(name) ?
        static_cast<std::string_view>(std::get<std::string>(name)) :
        type_to_name.left.find(std::get<ModelMetadataNames>(name))->second;
}

void read_name(ModelMetadata::NameType &name, std::string_view input) {
    auto name_to_type = type_to_name.right;
    auto it = name_to_type.find(input);
    if (it == name_to_type.end()) {
        name = std::string(input);
    } else {
        name = it->second;
    }
}

void write(std::ostream &stream, const CT_Metadata_Model &metadata, std::string_view indent) { 
    assert(is_valid(metadata));
    for (const ModelMetadata& m : metadata) {
        stream << indent;
        write_metadata(stream, m);
    }
}
} // namespace Slic3r
