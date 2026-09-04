#pragma once

#include <concepts>
#include <string_view>

namespace Slic3r::Biz::Yaml::Details {
enum class NodeType
{
    Scalar, Sequence, Mapping
};

// Mark is a transient location descriptor valid only while the source Document
// is alive.  ParseErrorDesc must not outlive the Document it was parsed from.
//
// file lifetime by adapter:
//   ryml    — string_view into ParserData::file, which is heap-allocated and
//             kept alive by Document::parser_data (shared_ptr).  Safe as long
//             as the Document outlives the ParseErrorDesc.
//   yaml-cpp — string_view into NodeRef::file, a std::string member of a
//              stack-allocated NodeRef.  UNSAFE once that NodeRef is destroyed;
//              yaml-cpp is retained only for compilation compatibility and must
//              not be used at runtime (all builds should use ryml).
struct Mark
{
    std::string_view file;
    size_t line{0};
    size_t column{0};
};

template <typename T, typename NodeRefT>
concept DocumentInterface = requires {
    { std::declval<T>() } -> std::convertible_to<bool>;
    { std::declval<T>().root() } -> std::same_as<NodeRefT>;
};

template <typename T>
concept NodeRefInterface = requires {
    { std::declval<T>() } -> std::convertible_to<bool>;
    { std::declval<const T>().is_null() } -> std::same_as<bool>;
};

template<typename T>
concept YamlAdapterInterface = requires(
    const char* filename,
    std::string_view yaml,
    const typename T::Parser& parser,
    const typename T::NodeRef& node,
    size_t index,
    std::string_view name,
    const typename T::KeyValuePair& pair,
    typename T::NodeRef& mutable_node,
    typename T::Emitter& emitter,
    std::string_view value
)
{
    typename T::NodeRef;
    typename T::Document;
    typename T::Parser;
    typename T::KeyValuePair;
    { T::create_file_parser(filename) } -> std::same_as<typename T::Parser>;
    { T::create_string_parser(yaml) } -> std::same_as<typename T::Parser>;
    { T::load(parser) } -> std::same_as<typename T::Document>;
    { T::node_type(node) } -> std::same_as<NodeType>;
    { T::scalar_value(node) } -> std::same_as<std::string_view>;
    { T::sequence_item_count(node) } -> std::same_as<size_t>;
    { T::sequence_item_at(node, index) } -> std::same_as<typename T::NodeRef>;
    { T::mapping_item_count(node) } -> std::same_as<size_t>;
    { T::mapping_value_at(node, name) } -> std::same_as<typename T::NodeRef>;
    { T::mapping_key_value_at(node, index) } -> std::same_as<typename T::KeyValuePair>;
    { T::key(pair, node) } -> std::same_as<typename T::NodeRef>;
    { T::value(pair, node) } -> std::same_as<typename T::NodeRef>;
    { T::mark(node) } -> std::same_as<Mark>;

    typename T::Emitter;
    //    static NodeRef create_scalar_node();
    { T::create_scalar_node(value) } -> std::same_as<typename T::NodeRef>;
    { T::create_null_node() } -> std::same_as<typename T::NodeRef>;
    //    static NodeRef create_sequence_node();
    { T::create_sequence_node() } -> std::same_as<typename T::NodeRef>;
    //    static void sequence_append(NodeRef& node, const NodeRef& item);
    { T::sequence_append(mutable_node, node) } -> std::same_as<void>;
    //    static NodeRef create_mapping_node();
    { T::create_mapping_node() } -> std::same_as<typename T::NodeRef>;
    //    static void mapping_append(NodeRef& node, std::string_view key, const NodeRef& value);
    { T::mapping_append(mutable_node, node, node) } -> std::same_as<void>;
    //    static Emitter create_emitter(const NodeRef& node);
    { T::create_emitter(node) } -> std::same_as<typename T::Emitter>;
    //    static std::string_view emitter_output(const Emitter& emitter);
    { T::emitter_output(emitter) } -> std::same_as<std::string_view>;
} && DocumentInterface<typename T::Document, typename T::NodeRef> && NodeRefInterface<typename T::NodeRef>;

}
