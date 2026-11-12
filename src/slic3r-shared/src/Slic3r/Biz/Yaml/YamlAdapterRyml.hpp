#pragma once
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <ryml.hpp>
#include <ryml_std.hpp>

#include "Slic3r/Biz/Yaml/YamlAdapter.hpp"

namespace Slic3r::Biz::Yaml::Ryml {
namespace Details {

// ---------------------------------------------------------------------------
// Serialize node types — used only on the write/serialisation path
// ---------------------------------------------------------------------------

struct SerializeNull
{};

struct SerializeScalar
{
    std::string value;
};

struct SerializeNode;
using SerializeNodePtr = std::shared_ptr<SerializeNode>;

struct SerializeSeq
{
    std::vector<SerializeNodePtr> items;
};

struct SerializeMap
{
    std::vector<std::pair<SerializeNodePtr, SerializeNodePtr>> items;
};

struct SerializeNode
{
    std::variant<SerializeNull, SerializeScalar, SerializeSeq, SerializeMap> data;
};

// ---------------------------------------------------------------------------
// ParserData — heap-allocated, shared between Parser and Document so the
// ryml::Tree (whose nodes contain raw pointers into `raw`) stays alive as long
// as any Document or NodeRef derived from this parse is in use.
//
// Member declaration order matters: handler must come before ryml_parser
// because C++ initialises in declaration order and ryml::Parser takes &handler.
// ---------------------------------------------------------------------------

struct ParserData
{
    std::string raw; // mutable YAML bytes for in-place parsing
    std::string file;
    ryml::EventHandlerTree handler; // must be before ryml_parser
    ryml::Parser ryml_parser;
    ryml::Tree tree;
    // Sorted byte offsets of every '\n' in raw (after in-place parse).
    // Used by mark() for O(log n) line/column lookup instead of O(n) scan.
    std::vector<size_t> newline_offsets;

    explicit ParserData(std::string raw_, std::string file_);
};

struct Parser
{
    std::shared_ptr<ParserData> data;
    mutable size_t current{0};
};

// ---------------------------------------------------------------------------
// NodeRef
//
// Parsed nodes  : `node` holds the ryml ConstNodeRef; `parser_data` (raw,
// non-owning) provides access to the ryml::Parser for location
// queries.  The owning handle (shared_ptr) lives in Document.
// Synthetic nodes: `synthetic` is set; everything else is irrelevant.
//
// `key_mode` lets scalar_value() return node->key() vs node->val() so that
// key() and value() can return distinct NodeRefs from the same ryml child node.
// ---------------------------------------------------------------------------

struct NodeRef
{
    // Parsed path
    std::optional<ryml::ConstNodeRef> node;
    ParserData* parser_data{nullptr}; // non-owning
    bool key_mode{false};

    // Synthetic path (write/serialisation only)
    SerializeNodePtr synthetic;

    operator bool() const
    {
        return synthetic ? true : node.has_value() && !node->invalid();
    }

    bool is_null() const
    {
        if (synthetic) {
            return std::holds_alternative<SerializeNull>(synthetic->data);
        }
        if (!node.has_value() || node->invalid()) {
            return false;
        }
        if (key_mode) {
            return false; // map keys are never YAML null in practice
        }
        if (!node->has_val() || node->is_val_quoted()) {
            return false;
        }
        const ryml::csubstr v = node->val();
        return v.empty() || v == "~" || v == "null";
    }
};

// ---------------------------------------------------------------------------
// Document — shares ownership of ParserData so the tree survives parse_file()
// ---------------------------------------------------------------------------

struct Document
{
    std::optional<ryml::ConstNodeRef> node;
    std::shared_ptr<ParserData> parser_data;

    operator bool() const
    {
        return node.has_value() && !node->invalid();
    }

    NodeRef root() const
    {
        return NodeRef{.node = node, .parser_data = parser_data.get()};
    }
};

// ---------------------------------------------------------------------------
// Emitter — holds the YAML text produced by create_emitter()
// ---------------------------------------------------------------------------

struct Emitter
{
    std::string output;
};

// A KeyValuePair is the ryml child node that carries both key and val parts.
using KeyValuePair = ryml::ConstNodeRef;

} // namespace Details

// ---------------------------------------------------------------------------
// Public adapter
// ---------------------------------------------------------------------------

struct YamlAdapterRyml
{
    using Parser       = Details::Parser;
    using Document     = Details::Document;
    using NodeRef      = Details::NodeRef;
    using KeyValuePair = Details::KeyValuePair;
    using Emitter      = Details::Emitter;

    // --- Parsing ---
    static Parser create_file_parser(const char* file_name);
    static Parser create_string_parser(std::string_view data);
    static Document load(const Parser& parser);

    // --- Node inspection ---
    static Yaml::Details::NodeType node_type(const NodeRef& node);
    static std::string_view scalar_value(const NodeRef& node);

    // --- Sequence ---
    static size_t sequence_item_count(const NodeRef& node);
    static NodeRef sequence_item_at(const NodeRef& node, size_t index);

    // O(N) sequence iteration — avoids the O(N²) cost of repeated index-based access
    // (ryml stores children as a linked list; operator[](i) traverses i steps each call)
    template <typename Func>
    static void for_each_sequence_item(const NodeRef& node, Func&& fn)
    {
        if (node.synthetic) {
            for (const auto& child : std::get<Details::SerializeSeq>(node.synthetic->data).items) {
                fn(NodeRef{.node = {}, .synthetic = child});
            }
        } else {
            for (const ryml::ConstNodeRef child : node.node->children()) {
                fn(NodeRef{.node = child, .parser_data = node.parser_data, .synthetic = {}});
            }
        }
    }

    // --- Mapping ---
    static size_t mapping_item_count(const NodeRef& node);
    static NodeRef mapping_value_at(const NodeRef& node, std::string_view name);
    static KeyValuePair mapping_key_value_at(const NodeRef& node, size_t index);
    static NodeRef key(const KeyValuePair& pair, const NodeRef& parent);
    static NodeRef value(const KeyValuePair& pair, const NodeRef& parent);

    // O(N) mapping iteration — avoids the O(N²) cost of repeated index-based access
    template <typename Func>
    static void for_each_mapping_item(const NodeRef& node, Func&& fn)
    {
        for (const ryml::ConstNodeRef child : node.node->children()) {
            fn(child);
        }
    }

    // --- Location ---
    static Yaml::Details::Mark mark(const NodeRef& node);

    // --- Write / serialisation ---
    static NodeRef create_scalar_node(std::string_view value);
    static NodeRef create_null_node();
    static NodeRef create_sequence_node();
    static void sequence_append(NodeRef& node, const NodeRef& item);
    static NodeRef create_mapping_node();
    static void mapping_append(NodeRef& node, const NodeRef& key, const NodeRef& value);
    static Emitter create_emitter(const NodeRef& node);
    static std::string_view emitter_output(const Emitter& emitter);
};

static_assert(Yaml::Details::YamlAdapterInterface<YamlAdapterRyml>);

} // namespace Slic3r::Biz::Yaml::Ryml
