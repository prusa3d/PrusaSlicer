#ifndef slic3r_Format_3mf_Metadata_hpp_
#define slic3r_Format_3mf_Metadata_hpp_

#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <sstream>

namespace Slic3r {

template<typename KnownMetadataNames> 
struct MetadataBase {
    using NameType = std::variant<KnownMetadataNames, std::string>;

    // For known name is used enum value otherwise original string name
    NameType name;

    // content of metadata tag
    std::string value;

    // A non-zero value indicates the producer wants the consumer to
    // preserve this value when it saves a modified version of this 3MF
    bool preserve = false;

    // A string indicating the XML type of the data stored in the metadata value
    std::string type; // optional

    // start with known names ordered by enum definition
    // continue alphanumericaly sorted unknown metadata
    bool operator<(const MetadataBase<KnownMetadataNames> &other) const {
        bool is_known = std::holds_alternative<KnownMetadataNames>(name);
        if (std::holds_alternative<std::string>(other.name)){
            return is_known ? false : (std::get<std::string>(name) < std::get<std::string>(other.name));
        }else{
            return is_known ? (std::get<KnownMetadataNames>(name) < std::get<KnownMetadataNames>(other.name)) : true;
        }
    }
};

// Order of enum will be same in stored 3mf document
// metadata supported in context of <model>
enum class ModelMetadataNames {
    // our defined metadata tag
    Slic3r_version, // Mayor version of stored data
    // higher number than current (3mf is future version) creates version warning
    // 3mf is processed as created in unknown application

    // Defined in 3MF core specification
    Title,            // A title for the 3MF document
    Designer,         // A name for a designer of this document
    Description,      // A description of the document
    Copyright,        // A copyright associated with this document
    LicenseTerms,     // License information associated with this document
    Rating,           // An industry rating associated with this document
    CreationDate,     // The date this documented was created by a source app
    ModificationDate, // The date this document was last modified Application
    Application       // The name of the source application that originally created this document
};
using ModelMetadata = MetadataBase<ModelMetadataNames>;
// Name mimic 3mf specification
// CT_ .. prefix for complex type
using CT_Metadata_Model = std::vector<ModelMetadata>;

std::string_view to_name(const ModelMetadata::NameType &name);
void read_name(ModelMetadata::NameType &name, std::string_view input);

// write / read functions
void write(std::ostream &stream, const CT_Metadata_Model &metadata, std::string_view indent = " ");

} // namespace Slic3r
#endif // slic3r_Format_3mf_Metadata_hpp_
