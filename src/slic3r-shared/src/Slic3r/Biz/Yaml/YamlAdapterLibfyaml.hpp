#pragma once

#include "Slic3r/Biz/Yaml/YamlAdapter.hpp"

#include <string>
#include <string_view>
#include <memory>

#include <libfyaml.h>

namespace Slic3r::Biz::Yaml::Libfyaml {

namespace Details {
struct DocumentDeleter
{
    void operator()(fy_document* o) { fy_document_destroy(o); }
};

struct ParserDeleter
{
    void operator()(fy_parser* o) { fy_parser_destroy(o); }
};

struct NodeRef
{
    fy_node* node;
    std::string_view file;

    operator bool() const { return node != nullptr; }
    bool is_null() const { return node != nullptr && fy_node_is_null(node); }
};

using DocumentPtr = std::unique_ptr<fy_document, DocumentDeleter>;

struct Document
{
    DocumentPtr doc;
    std::string file;

    NodeRef root() const { return {fy_document_root(doc.get()), file}; }
    operator bool() const { return doc.get() != nullptr; }
};

using ParserPtr = std::unique_ptr<fy_parser, Details::ParserDeleter>;

struct Parser
{
    ParserPtr parser;
    std::string file;
};

} // namespace Details

struct YamlAdapterLibfyaml
{
    using NodeRef = Details::NodeRef;
    using Document = Details::Document;
    using Parser = Details::Parser;
    using KeyValuePair = fy_node_pair*;

    static Parser create_file_parser(const char* file_name);
    static Parser create_string_parser(std::string_view data);

    static Document load(const Parser& parser);

    static Yaml::Details::NodeType node_type(const NodeRef& node);

    static std::string_view scalar_value(const NodeRef& node);

    static size_t sequence_item_count(const NodeRef& node);
    static NodeRef sequence_item_at(const NodeRef& node, size_t index);

    template<typename Func>
    static void for_each_sequence_item(const NodeRef& node, Func&& fn)
    {
        void* iter = nullptr;
        fy_node* item;
        while ((item = fy_node_sequence_iterate(node.node, &iter)) != nullptr)
            fn(NodeRef{item, node.file});
    }

    static size_t mapping_item_count(const NodeRef& node);
    static NodeRef mapping_value_at(const NodeRef& node, std::string_view name);
    static KeyValuePair mapping_key_value_at(const NodeRef& node, size_t index);
    static NodeRef key(const KeyValuePair& pair, const NodeRef& parent);
    static NodeRef value(const KeyValuePair& pair, const NodeRef& parent);

    template <typename Func>
    static void for_each_mapping_item(const NodeRef& node, Func&& fn)
    {
        void* iter = nullptr;
        fy_node_pair* pair;
        while ((pair = fy_node_mapping_iterate(node.node, &iter)) != nullptr) {
            fn(pair);
        }
    }

    static Yaml::Details::Mark mark(const NodeRef& node);
};

static_assert(Yaml::Details::YamlAdapterInterface<YamlAdapterLibfyaml>);

}
