#include "Relations.hpp"
#include "Slic3r/Utils.hpp"
#include "pugixml.hpp"
#include <Slic3r/Log.hpp>
#include "boost/filesystem.hpp"

#include "tl/expected.hpp"

namespace{
using namespace Slic3r;

constexpr const char *RELATIONSHIPS = "Relationships";
constexpr const char *XMLNS_ATTR = "xmlns";
constexpr const char *XMLNS_VAL = "http://schemas.openxmlformats.org/package/2006/relationships";

constexpr const char *RELATIONSHIP = "Relationship";
constexpr const char *TARGET_ATTR = "Target";
constexpr const char *TYPE_ATTR = "Type";
constexpr const char *ID_ATTR = "Id";

constexpr const char *MODEL_TYPE =     "http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel";
constexpr const char *THUMBNAIL_TYPE = "http://schemas.openxmlformats.org/package/2006/relationships/metadata/thumbnail";
constexpr const char *PROJECT_TYPE =   "http://schemas.prusa3d.cz/package/2024/relationships/metadata/projectfile";

constexpr const char *MODEL_DEFAULT_PATH = "3D/3dmodel.model";

static void append_realationship(const pugi::xml_node &node, std::optional<RootRelations>& relations, Read3mfIssues& collected_issues)
{
    struct{
        const char *type = nullptr;
        const char *id = nullptr;
        const char *target = nullptr;
    }r;
    for (const pugi::xml_attribute &attr : node.attributes()) {
        if (std::strcmp(attr.name(), TYPE_ATTR) == 0) {
            r.type = attr.value();
        } else if (std::strcmp(attr.name(), ID_ATTR) == 0) {
            r.id = attr.value();
        } else if (std::strcmp(attr.name(), TARGET_ATTR) == 0) {
            r.target = attr.value();
        } else {
            collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::relation_unknown_attr,
                std::string(attr.name()), + attr.as_string() ));
        }
    }
    if (r.type == nullptr) {
        auto issue = Read3mfIssue(Read3mfIssueType::relation_should_have_type);
        collected_issues.add_issue(issue);
        return;
    }

    if (r.id == nullptr)
        collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::relation_should_have_id));

    auto get_relations = [&relations]() -> RootRelations & {
        if (! relations.has_value())
            relations = RootRelations{{}, {}}; // Initiazlize on empty
        return *relations;
    };

    if (std::strcmp(r.type, MODEL_TYPE) == 0) {
        if (r.target != nullptr)
            get_relations().main_model_path = std::string(r.target);
        else
            collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::relation_model_without_target));
    } else if (std::strcmp(r.type, THUMBNAIL_TYPE) == 0) {
        if (r.target != nullptr)
            get_relations().thumbnail_path = std::string(r.target);
        else
            collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::relation_thumbnail_without_target));
    } else if (std::strcmp(r.type, PROJECT_TYPE) == 0) {
        if (r.target != nullptr)
            get_relations().project_file_path = std::string(r.target);
        else
            collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::relation_project_without_target));
    } else {
        SPDLOG_INFO("Not known realtions (Type={}, Target={}, Id={})", r.type, r.target, r.id);
        collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::relation_unexpected_type, 
            std::nullopt, std::string(r.type) + " " + std::string(r.target) + " " + std::string(r.id)));
    }
}

tl::expected<LoadedRelations, Read3mfIssue> load(const pugi::xml_document& doc, Read3mfIssues& collected_issues) {
    const pugi::xml_node root = doc.child(RELATIONSHIPS);
    if (root.empty()) {
        std::string name{doc.first_child().name()};
        return tl::make_unexpected(Read3mfIssue(Read3mfIssueType::relations_missing_root, name));
    }    

    
    for (const pugi::xml_attribute &attr : root.attributes()) {
        if (std::strcmp(attr.name(), XMLNS_ATTR) == 0) {
            if (std::strcmp(attr.value(), XMLNS_VAL) != 0)
                collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::relations_unexpected_xmlns, attr.as_string()));
        } else {
            collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::relations_unknown_attr,
                std::string(attr.name()), attr.as_string()));
        }
    }
    
    LoadedRelations result;

    for (const pugi::xml_node &node : root.children()) {
        if (std::strcmp(node.name(), RELATIONSHIP) == 0) {
            append_realationship(node, result.relations, collected_issues);
        } else {
            collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::relations_unknown_node, std::string(node.name())));
        }
    }
    
    if (!result.relations.has_value() || result.relations->main_model_path.empty())
        collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::relation_missing_main_model));
    
    if (!result.relations.has_value() || result.relations->thumbnail_path.empty())
        collected_issues.add_issue(Read3mfIssue(Read3mfIssueType::relation_missing_thumbnail));

    return result;
}

void store_relationship(std::stringstream& stream, const std::string &target, const std::string &type, const std::string &id) 
{
    stream << " <" << RELATIONSHIP << " " 
        << TARGET_ATTR << "=\"/" << target << "\" "
        << ID_ATTR << "=\"" << id << "\" " 
        << TYPE_ATTR << "=\"" << type << "\"/>\n";
}

}

namespace Slic3r{

Relationships get_relationships(const RootRelations &root_relations) {
    Relationships r;
    if (!root_relations.main_model_path.empty())
        r.push_back({root_relations.main_model_path, MODEL_TYPE});
    if (!root_relations.thumbnail_path.empty())
        r.push_back({root_relations.thumbnail_path, THUMBNAIL_TYPE});
    if (!root_relations.project_file_path.empty())
        r.push_back({root_relations.project_file_path, PROJECT_TYPE});
    return r;
}

Relationships get_relationships(const std::vector<std::string> &model_paths) {
    Relationships r;
    r.reserve(model_paths.size());
    for (const std::string& model_path: model_paths)
        r.push_back({model_path, MODEL_TYPE});
    return r;
}

void store(mz_zip_archive &archive, const Relationships &relationships, const char *filepath) {
    assert(!relationships.empty());
    // write xml with Relations
    std::stringstream stream;
    stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    stream << "<" << RELATIONSHIPS << " " << XMLNS_ATTR << "=\"" << XMLNS_VAL << "\">\n";
    int counter = 0;
    for (Relationship r : relationships)
        store_relationship(stream, r.target, r.type, "rel-"+std::to_string(++counter));
    stream << "</" << RELATIONSHIPS << ">";
    std::string out = stream.str();
    if (!mz_zip_writer_add_mem(&archive, filepath, (const void*)out.data(), out.length(), MZ_DEFAULT_COMPRESSION))
        throw boost::filesystem::filesystem_error("Unable to add relationships file to archive.", {});
}

tl::expected<LoadedRelations, Read3mfIssue> load_relations(mz_zip_archive &archive, const char * filepath, Read3mfIssues& collected_issues) {
    int index = mz_zip_reader_locate_file(&archive, filepath, nullptr, 0);
    if (index < 0)
        return tl::make_unexpected(Read3mfIssue(Read3mfIssueType::relations_missing));

    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&archive, index, &stat))
        return tl::make_unexpected(Read3mfIssue(Read3mfIssueType::relations_unreadable));

    if (stat.m_uncomp_size == 0)
        return tl::make_unexpected(Read3mfIssue(Read3mfIssueType::relations_bad_size));

    size_t uncomp_size = static_cast<size_t>(stat.m_uncomp_size);
    char *buffer = static_cast<char *>(pugi::get_memory_allocation_function()(uncomp_size));
    if (buffer == nullptr)
        return tl::make_unexpected(Read3mfIssue(Read3mfIssueType::relations_no_memmory));
    ScopeGuard sc_buffer([buffer]() { pugi::get_memory_deallocation_function()(buffer); });

    if (mz_zip_reader_extract_to_mem(&archive, index, buffer, uncomp_size, 0) != MZ_TRUE)
        return tl::make_unexpected(Read3mfIssue(Read3mfIssueType::relations_cant_extract));

    pugi::xml_document doc;
    pugi::xml_parse_result parse_result = doc.load_buffer_inplace(buffer, uncomp_size);
    if (parse_result.status != pugi::xml_parse_status::status_ok) {
        SPDLOG_ERROR("Pugi can't load Relations xml from given data for Relations: {}", parse_result.description());
        std::string description = parse_result.description();
        return tl::make_unexpected(Read3mfIssue(Read3mfIssueType::relations_pugi_error, description));
    }

    auto result = load(doc, collected_issues);
    if (result.has_value())
        result.value().realtions_file_index = index;
    return result;
}

// return null terminated path to model

const char * LoadedRelations::get_main_model_path() const {
    const char *main_model_path = (relations.has_value() && !relations->main_model_path.empty()) ?
        relations->main_model_path.c_str() : MODEL_DEFAULT_PATH;
    if (main_model_path[0] == '/')
        main_model_path += 1; // skip first slash
    return main_model_path;
}

} // namespace Slic3r
