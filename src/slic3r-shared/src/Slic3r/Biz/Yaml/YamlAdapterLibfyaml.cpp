#include "Slic3r/Biz/Yaml/YamlAdapterLibfyaml.hpp"

#include "Slic3r/Assert.hpp"
#include <fmt/format.h>

namespace Slic3r::Biz::Yaml::Libfyaml {

static Details::ParserPtr create_parser()
{
    fy_parse_cfg cfg = {
        .search_path = "",
        .flags = FYPCF_QUIET,
        .userdata = nullptr,
        .diag = nullptr,
    };

    return Details::ParserPtr{fy_parser_create(&cfg)};
}

YamlAdapterLibfyaml::Parser YamlAdapterLibfyaml::create_file_parser(const char* file_name)
{
    auto parser = create_parser();
    fy_parser_set_input_file(parser.get(), file_name);
    return {std::move(parser), file_name};
}

YamlAdapterLibfyaml::Parser YamlAdapterLibfyaml::create_string_parser(std::string_view yaml)
{
    auto parser = create_parser();
    fy_parser_set_string(parser.get(), yaml.data(), yaml.length());
    return {std::move(parser), "<string>"};
}

YamlAdapterLibfyaml::Document YamlAdapterLibfyaml::load(const Parser& parser)
{
    return Document{Details::DocumentPtr{fy_parse_load_document(parser.parser.get())}, parser.file};
}


Yaml::Details::NodeType YamlAdapterLibfyaml::node_type(const NodeRef& node)
{
    auto type = fy_node_get_type(node.node);
    switch (type) {
    case FYNT_SCALAR:
        return Yaml::Details::NodeType::Scalar;

    case FYNT_SEQUENCE:
        return Yaml::Details::NodeType::Sequence;
    case FYNT_MAPPING:
        return Yaml::Details::NodeType::Mapping;
    }
    PANIC("Unknown YAML node type", type);
}

std::string_view YamlAdapterLibfyaml::scalar_value(const NodeRef& node)
{
    size_t len;
    return {fy_node_get_scalar(node.node, &len), len};
}

size_t YamlAdapterLibfyaml::sequence_item_count(const NodeRef& node)
{
    return fy_node_sequence_item_count(node.node);
}

YamlAdapterLibfyaml::NodeRef YamlAdapterLibfyaml::sequence_item_at(const NodeRef& node, size_t index)
{
    return NodeRef{fy_node_sequence_get_by_index(node.node, index), node.file};
}

size_t YamlAdapterLibfyaml::mapping_item_count(const NodeRef& node)
{
    return fy_node_mapping_item_count(node.node);
}

YamlAdapterLibfyaml::NodeRef YamlAdapterLibfyaml::mapping_value_at(const NodeRef& node, std::string_view name)
{
    return NodeRef{fy_node_mapping_lookup_value_by_simple_key(node.node, name.data(), name.length()), node.file};
}

YamlAdapterLibfyaml::KeyValuePair YamlAdapterLibfyaml::mapping_key_value_at(const NodeRef& node, size_t index)
{
    return fy_node_mapping_get_by_index(node.node, index);
}

YamlAdapterLibfyaml::NodeRef YamlAdapterLibfyaml::key(const KeyValuePair& pair, const NodeRef& parent)
{
    return NodeRef{fy_node_pair_key(pair), parent.file};
}

YamlAdapterLibfyaml::NodeRef YamlAdapterLibfyaml::value(const KeyValuePair& pair, const NodeRef& parent)
{
    return NodeRef{fy_node_pair_value(pair), parent.file};
}

Yaml::Details::Mark YamlAdapterLibfyaml::mark(const NodeRef& node)
{
    if (node.node == nullptr)
        return {.file=node.file};
    auto* token = fy_node_get_start_token(node.node);
    auto* mark = fy_token_start_mark(token);
    return {.file=node.file, .line=mark->line + 1, .column=mark->column + 1};
}

}