#include "Slic3r/Biz/Yaml/YamlAdapterRyml.hpp"

#include <algorithm>
#include <stdexcept>

#include <boost/nowide/fstream.hpp>

namespace Slic3r::Biz::Yaml::Ryml {

using Details::ParserData;
using Details::SerializeMap;
using Details::SerializeNode;
using Details::SerializeNodePtr;
using Details::SerializeNull;
using Details::SerializeScalar;
using Details::SerializeSeq;

// ---------------------------------------------------------------------------
// ParserData constructor
//
// handler must be constructed before ryml_parser (declaration order), and the
// tree is populated in the constructor body once ryml_parser is ready.
// make_shared ensures ParserData never moves, so &handler stays valid.
// ---------------------------------------------------------------------------

Details::ParserData::ParserData(std::string raw_, std::string file_) :
    raw{std::move(raw_)},
    file{std::move(file_)},
    handler{},
    ryml_parser{&handler}
{
    tree = ryml::parse_in_place(
        &ryml_parser,
        ryml::csubstr{this->file.data(), this->file.size()},
        ryml::substr{this->raw.data(), this->raw.size()}
    );
    // Build newline table after parsing (ryml may rewrite the buffer in-place,
    // so scan post-parse to match the same bytes that val().str points into).
    for (size_t i = 0; i < this->raw.size(); ++i) {
        if (this->raw[i] == '\n') {
            newline_offsets.push_back(i);
        }
    }
}

// ---------------------------------------------------------------------------
// Parser factory helpers
// ---------------------------------------------------------------------------

YamlAdapterRyml::Parser YamlAdapterRyml::create_file_parser(const char* file_name)
{
    boost::nowide::ifstream fs(file_name, std::ios::binary);
    if (!fs)
        throw std::runtime_error(std::string{"Cannot open file: "} + file_name);
    std::string raw(std::istreambuf_iterator<char>(fs), {});
    return Parser{std::make_shared<ParserData>(std::move(raw), std::string{file_name}), 0};
}

YamlAdapterRyml::Parser YamlAdapterRyml::create_string_parser(std::string_view data)
{
    return Parser{std::make_shared<ParserData>(std::string{data}, std::string{"<string>"}), 0};
}

// ---------------------------------------------------------------------------
// Document loading
//
// ryml wraps multiple documents in a STREAM root node.  Single-document files
// have a regular root.  We advance parser.current to allow iterating all docs.
// ---------------------------------------------------------------------------

YamlAdapterRyml::Document YamlAdapterRyml::load(const Parser& parser)
{
    const ryml::Tree& tree      = parser.data->tree;
    const ryml::id_type root_id = tree.root_id();

    if (tree.is_stream(root_id)) {
        const size_t num_docs = tree.num_children(root_id);
        if (parser.current >= num_docs)
            return Document{};
        return Document{
            .node        = tree.docref(parser.current++),
            .parser_data = parser.data,
        };
    }

    // Single document
    if (parser.current > 0) {
        return Document{};
    }
    ++parser.current;
    return Document{
        .node        = tree.crootref(),
        .parser_data = parser.data,
    };
}

// ---------------------------------------------------------------------------
// Node inspection
// ---------------------------------------------------------------------------

Yaml::Details::NodeType YamlAdapterRyml::node_type(const NodeRef& node)
{
    if (node.synthetic) {
        return std::visit(
            [](const auto& v)
            {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, SerializeMap>)
                    return Yaml::Details::NodeType::Mapping;
                if constexpr (std::is_same_v<T, SerializeSeq>)
                    return Yaml::Details::NodeType::Sequence;
                return Yaml::Details::NodeType::Scalar;
            },
            node.synthetic->data
        );
    }
    if (node.key_mode) {
        return Yaml::Details::NodeType::Scalar;
    }
    if (node.node->is_map()) {
        return Yaml::Details::NodeType::Mapping;
    }
    if (node.node->is_seq()) {
        return Yaml::Details::NodeType::Sequence;
    }
    return Yaml::Details::NodeType::Scalar;
}

std::string_view YamlAdapterRyml::scalar_value(const NodeRef& node)
{
    if (node.synthetic) {
        const auto& s = std::get<SerializeScalar>(node.synthetic->data);
        return s.value;
    } else {
        const ryml::csubstr s = node.key_mode ? node.node->key() : node.node->val();
        return {s.str, s.len};
    }
}

// ---------------------------------------------------------------------------
// Sequence
// ---------------------------------------------------------------------------

size_t YamlAdapterRyml::sequence_item_count(const NodeRef& node)
{
    if (node.synthetic) {
        return std::get<SerializeSeq>(node.synthetic->data).items.size();
    } else {
        return node.node->num_children();
    }
}

YamlAdapterRyml::NodeRef YamlAdapterRyml::sequence_item_at(const NodeRef& node, size_t index)
{
    if (node.synthetic) {
        return NodeRef{.synthetic = std::get<SerializeSeq>(node.synthetic->data).items[index]};
    } else {
        return NodeRef{
            .node        = (*node.node)[index],
            .parser_data = node.parser_data,
        };
    }
}

// ---------------------------------------------------------------------------
// Mapping
// ---------------------------------------------------------------------------

size_t YamlAdapterRyml::mapping_item_count(const NodeRef& node)
{
    if (node.synthetic) {
        return std::get<SerializeMap>(node.synthetic->data).items.size();
    } else {
        return node.node->num_children();
    }
}

YamlAdapterRyml::NodeRef
YamlAdapterRyml::mapping_value_at(const NodeRef& node, std::string_view name)
{
    if (node.synthetic) {
        const auto& m = std::get<SerializeMap>(node.synthetic->data);
        for (const auto& [k, v] : m.items) {
            if (std::get<SerializeScalar>(k->data).value == name)
                return NodeRef{.synthetic = v};
        }
        return NodeRef{};
    } else {
        const ryml::csubstr key{name.data(), name.size()};
        const ryml::ConstNodeRef child = node.node->find_child(key);
        if (child.invalid()) {
            return NodeRef{};
        }
        return NodeRef{
            .node        = child,
            .parser_data = node.parser_data,
        };
    }
}

YamlAdapterRyml::KeyValuePair
YamlAdapterRyml::mapping_key_value_at(const NodeRef& node, size_t index)
{
    return (*node.node)[index];
}

YamlAdapterRyml::NodeRef YamlAdapterRyml::key(const KeyValuePair& pair, const NodeRef& parent)
{
    return NodeRef{
        .node        = pair,
        .parser_data = parent.parser_data,
        .key_mode    = true,
    };
}

YamlAdapterRyml::NodeRef YamlAdapterRyml::value(const KeyValuePair& pair, const NodeRef& parent)
{
    return NodeRef{
        .node        = pair,
        .parser_data = parent.parser_data,
        .key_mode    = false,
    };
}

// ---------------------------------------------------------------------------
// Location
// ---------------------------------------------------------------------------

Yaml::Details::Mark YamlAdapterRyml::mark(const NodeRef& node)
{
    if (node.synthetic || !node.node.has_value() || node.node->invalid() || !node.parser_data) {
        return {};
    }
    const ryml::ConstNodeRef& n = *node.node;
    const std::string& raw      = node.parser_data->raw;
    const char* ptr             = nullptr;
    if (node.key_mode) {
        if (n.has_key())
            ptr = n.key().str;
    } else {
        if (n.has_val())
            ptr = n.val().str;
    }
    if (!ptr || ptr < raw.data() || ptr > raw.data() + raw.size()) {
        return {};
    }
    const size_t offset = static_cast<size_t>(ptr - raw.data());
    const auto& nl      = node.parser_data->newline_offsets;
    // upper_bound gives the first newline at or after offset, so the count
    // of newlines before offset is the line index (0-based → +1 for 1-based).
    const size_t preceding =
        static_cast<size_t>(std::upper_bound(nl.begin(), nl.end(), offset) - nl.begin());
    const size_t line = 1 + preceding;
    const size_t col  = preceding == 0 ? offset : offset - nl[preceding - 1] - 1;
    return {std::string_view{node.parser_data->file}, line, col};
}

YamlAdapterRyml::NodeRef YamlAdapterRyml::create_scalar_node(std::string_view value)
{
    return NodeRef{
        .synthetic =
            std::make_shared<SerializeNode>(SerializeNode{SerializeScalar{std::string{value}}})
    };
}

YamlAdapterRyml::NodeRef YamlAdapterRyml::create_null_node()
{
    return NodeRef{.synthetic = std::make_shared<SerializeNode>(SerializeNode{SerializeNull{}})};
}

YamlAdapterRyml::NodeRef YamlAdapterRyml::create_sequence_node()
{
    return NodeRef{.synthetic = std::make_shared<SerializeNode>(SerializeNode{SerializeSeq{}})};
}

void YamlAdapterRyml::sequence_append(NodeRef& node, const NodeRef& item)
{
    std::get<SerializeSeq>(node.synthetic->data).items.push_back(item.synthetic);
}

YamlAdapterRyml::NodeRef YamlAdapterRyml::create_mapping_node()
{
    return NodeRef{.synthetic = std::make_shared<SerializeNode>(SerializeNode{SerializeMap{}})};
}

void YamlAdapterRyml::mapping_append(NodeRef& node, const NodeRef& key, const NodeRef& value)
{
    std::get<SerializeMap>(node.synthetic->data).items.emplace_back(key.synthetic, value.synthetic);
}

// ---------------------------------------------------------------------------
// Emitter — build a ryml::Tree from the SyntheticNode tree, then emit it.
//
// The SyntheticNodes (and therefore the std::strings they contain) outlive the
// ryml::Tree because node.synthetic is still alive during emitrs_yaml.
// ---------------------------------------------------------------------------

namespace {
void build_ryml_node(ryml::NodeRef out, const SerializeNode& syn)
{
    std::visit(
        [&](const auto& v)
        {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, SerializeNull>) {
                out.set_val(ryml::csubstr{"null", 4});
            } else if constexpr (std::is_same_v<T, SerializeScalar>) {
                out.set_val(ryml::csubstr{v.value.data(), v.value.size()});
            } else if constexpr (std::is_same_v<T, SerializeSeq>) {
                out |= ryml::SEQ;
                for (const auto& item : v.items) {
                    build_ryml_node(out.append_child(), *item);
                }
            } else if constexpr (std::is_same_v<T, SerializeMap>) {
                out |= ryml::MAP;
                for (const auto& [k, val] : v.items) {
                    const auto& key_scalar = std::get<SerializeScalar>(k->data);
                    ryml::NodeRef child    = out.append_child();
                    child.set_key(ryml::csubstr{key_scalar.value.data(), key_scalar.value.size()});
                    build_ryml_node(child, *val);
                }
            }
        },
        syn.data
    );
}
} // namespace

YamlAdapterRyml::Emitter YamlAdapterRyml::create_emitter(const NodeRef& node)
{
    ryml::Tree tree;
    build_ryml_node(tree.rootref(), *node.synthetic);
    return Emitter{ryml::emitrs_yaml<std::string>(tree)};
}

std::string_view YamlAdapterRyml::emitter_output(const Emitter& emitter)
{
    return emitter.output;
}

} // namespace Slic3r::Biz::Yaml::Ryml
