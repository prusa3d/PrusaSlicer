#include <boost/iostreams/stream.hpp>
#include "Slic3r/Biz/Yaml/YamlAdapterYamlCpp.hpp"

#include <boost/nowide/config.hpp>
#include <boost/nowide/fstream.hpp>

namespace Slic3r::Biz::Yaml::YamlCpp {

YamlAdapterYamlCpp::Parser YamlAdapterYamlCpp::create_file_parser(const char* file_name)
{
    boost::nowide::ifstream fs;
    fs.open(file_name, std::ios::in | std::ios::binary);
    if (!fs.good())
        throw std::runtime_error(std::string{"Bad file: "} + file_name);
    return {.nodes = YAML::LoadAll(fs), .file = file_name};
}

YamlAdapterYamlCpp::Parser YamlAdapterYamlCpp::create_string_parser(std::string_view data)
{
    boost::iostreams::stream<boost::iostreams::array_source> stream{data.data(), data.size()};
    return {.nodes = YAML::LoadAll(stream), .file = "<string>"};
}

YamlAdapterYamlCpp::Document YamlAdapterYamlCpp::load(const Parser& parser)
{
    if (parser.current < parser.nodes.size()) {
        const auto node = parser.nodes[parser.current++];
        return {.node = node, .file = parser.file};
    }
    return {.node = std::nullopt, .file = parser.file};
}

Yaml::Details::NodeType YamlAdapterYamlCpp::node_type(const NodeRef& node)
{
    auto type = node.node->Type();
    switch (type) {
    case YAML::NodeType::Map:
        return Yaml::Details::NodeType::Mapping;
    case YAML::NodeType::Sequence:
        return Yaml::Details::NodeType::Sequence;
    default:
        return Yaml::Details::NodeType::Scalar;
    }
}

std::string_view YamlAdapterYamlCpp::scalar_value(const NodeRef& node)
{
    return node.node->Scalar();
}

size_t YamlAdapterYamlCpp::sequence_item_count(const NodeRef& node)
{
    return node.node->size();
}

YamlAdapterYamlCpp::NodeRef YamlAdapterYamlCpp::sequence_item_at(const NodeRef& node, size_t index)
{
    return {.node = (*node.node)[index], .file = node.file};
}

size_t YamlAdapterYamlCpp::mapping_item_count(const NodeRef& node)
{
    return node.node->size();
}

YamlAdapterYamlCpp::NodeRef YamlAdapterYamlCpp::mapping_value_at(const NodeRef& node, std::string_view name)
{
    try {
#if defined(_MSC_VER)
        // For some reason MSVC has problems with string_view, lets allocate string for it
        std::string key{name};
#else
        auto key = name;
#endif
        return {.node = (*node.node)[key], .file = node.file};
    } catch (YAML::KeyNotFound&) {
        return {.node = std::nullopt, .file = node.file};
    }
}

YamlAdapterYamlCpp::KeyValuePair YamlAdapterYamlCpp::mapping_key_value_at(const NodeRef& node, size_t index)
{
    auto it = node.node->begin();
    std::advance(it, index);
    return it;
}

YamlAdapterYamlCpp::NodeRef YamlAdapterYamlCpp::key(const KeyValuePair& pair, const NodeRef& parent)
{
    return {.node = pair->first, .file = parent.file};
}

YamlAdapterYamlCpp::NodeRef YamlAdapterYamlCpp::value(const KeyValuePair& pair, const NodeRef& parent)
{
    return {.node = pair->second, .file = parent.file};
}

Yaml::Details::Mark YamlAdapterYamlCpp::mark(const NodeRef& node)
{
    if (!node.node.has_value())
        return {.file = node.file};
    auto mark = node.node->Mark();
    return {.file = node.file, .line = size_t(mark.line + 1), .column = size_t(mark.column + 1)};
}


YamlAdapterYamlCpp::NodeRef YamlAdapterYamlCpp::create_scalar_node(std::string_view value)
{
    NodeRef ret;
    ret.node = std::string{value};
    return ret;
}

YamlAdapterYamlCpp::NodeRef YamlAdapterYamlCpp::create_null_node()
{
    return {.node=YAML::Node{YAML::NodeType::Null}};
}

YamlAdapterYamlCpp::NodeRef YamlAdapterYamlCpp::create_sequence_node()
{
    return {.node=YAML::Node{YAML::NodeType::Sequence}};
}

void YamlAdapterYamlCpp::sequence_append(NodeRef& node, const NodeRef& item)
{
    node.node.value().push_back(item.node.value());
}

YamlAdapterYamlCpp::NodeRef YamlAdapterYamlCpp::create_mapping_node()
{
    return {.node=YAML::Node{YAML::NodeType::Map}};
}

void YamlAdapterYamlCpp::mapping_append(NodeRef& node, const NodeRef& key, const NodeRef& value)
{
    node.node.value()[*key.node] = value.node.value();
}

YamlAdapterYamlCpp::Emitter YamlAdapterYamlCpp::create_emitter(const NodeRef& node)
{
    Emitter emitter;
    emitter.emitter = std::make_unique<YAML::Emitter>();
    *emitter.emitter << YAML::LowerNull;
    *emitter.emitter << node.node.value();
    return emitter;
}

std::string_view YamlAdapterYamlCpp::emitter_output(const Emitter& emitter)
{
    return emitter.emitter->c_str();
}

} // namespace Slic3r::Biz::Yaml::YamlCpp
