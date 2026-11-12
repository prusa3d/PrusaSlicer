#pragma once

#include <stdexcept>
#include <functional>
#include <cstddef>
#include <map>
#include <unordered_map>
#include <memory>
#include <utility>
#include <variant>
#include <vector>
#include <optional>
#include <ranges>
#include <utility>
#include <string_view>
#include <fstream>

#include <boost/nowide/fstream.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/transform.hpp>
#include <boost/preprocessor/seq/enum.hpp>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/tuple.hpp>
#include <charconv>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <magic_enum/magic_enum.hpp>

#include "Slic3r/expected.hpp"
#include "Slic3r/Domain/TemplateUtils.hpp"

#if defined(SLIC3R_YAML_LIBFYAML)
#include "YamlAdapterLibfyaml.hpp"
#include "fast_float.h"
#elif defined(SLIC3R_YAML_YAMLCPP)
#include "YamlAdapterYamlCpp.hpp"
#include "fast_float.h"
#elif defined(SLIC3R_YAML_RYML)
#include "YamlAdapterRyml.hpp"
#include <c4/charconv.hpp>  // provides fast_float::from_chars
#else
#error "No YAML library selected"
#endif

namespace Slic3r::Biz::Yaml {

#if defined(SLIC3R_YAML_LIBFYAML)
using YamlAdapter = Libfyaml::YamlAdapterLibfyaml;
#elif defined(SLIC3R_YAML_YAMLCPP)
using YamlAdapter = YamlCpp::YamlAdapterYamlCpp;
#elif defined(SLIC3R_YAML_RYML)
using YamlAdapter = Ryml::YamlAdapterRyml;
#else
#error "No YAML library selected"
#endif

/**
 * @brief Parse YAML from file
 * @param file_name A file to parse
 * @return A parsed YAML document
 */
YamlAdapter::Document parse_file(const char* file_name);

/**
 * @brief Parse YAML from string
 * @param yaml YAML source as std::string_view
 * @return A parsed YAML document
 */
YamlAdapter::Document parse_string(std::string_view yaml);

/**
 * @brief Parse all YAML documents from file
 * @param file_name A file to parse
 * @param parse_doc A parse function to be called for parsed each document
 */
void parse_all_documents_in_file(
    const char* file_name,
    const std::function<void(const YamlAdapter::Document&)>& parse_doc
);

/**
 * @brief Parse all YAML documents from file
 * @param yaml YAML source as std::string_view
 * @param parse_doc A parse function to be called for parsed each document
 */
void parse_all_documents_in_string(
    std::string_view yaml,
    const std::function<void(const YamlAdapter::Document&)>& parse_doc
);

namespace Details {

// Only called at actual throw sites — NOT in ParseErrorDesc constructor.
inline std::string describe_node(const YamlAdapter::NodeRef& node)
{
    auto mark = YamlAdapter::mark(node);
    return fmt::format("[file: {}:{}:{}]", mark.file, mark.line, mark.column);
}

inline std::string format_error(const Details::Mark& mark, std::string_view message)
{
    return fmt::format("[file: {}:{}:{}]: {}", mark.file, mark.line, mark.column, message);
}

} // namespace Details

// ParseErrorDesc stores the raw Mark (file is a string_view into ParserData::file,
// valid for the entire parsing call stack) and defers fmt::format to the throw site.
// This avoids ~13 fmt::format calls per PresetValue probe that are immediately discarded.
struct ParseErrorDesc
{
    Details::Mark mark;
    std::string   message;

    ParseErrorDesc(const YamlAdapter::NodeRef& node, std::string message)
        : mark{YamlAdapter::mark(node)}
        , message{std::move(message)}
    {}
};

template <typename T>
using Result      = expected<T, ParseErrorDesc>;
using ResultError = unexpected<ParseErrorDesc>;

struct ParseError : std::runtime_error
{
    ParseError(const YamlAdapter::NodeRef& node, const std::string& msg) :
        std::runtime_error(Details::describe_node(node) + ": " + msg)
    {}

    explicit ParseError(const ParseErrorDesc& desc) :
        std::runtime_error(Details::format_error(desc.mark, desc.message))
    {}

    ParseError(const ParseError&) = default;
    ParseError(ParseError&&)      = default;

    ParseError& operator=(const ParseError&) = default;
    ParseError& operator=(ParseError&&)      = default;

    ParseError decorate_with_source(const std::string& source) const
    {
        return ParseError("[" + source + "] " + what());
    }

private:
    explicit ParseError(const std::string& msg) : std::runtime_error(msg) {}

    explicit ParseError(const char* msg) : std::runtime_error(msg) {}
};

struct SerializationError : std::runtime_error
{
    std::string file_name;

    SerializationError(const std::string& msg, const char* file_name) :
        std::runtime_error(msg),
        file_name(file_name)
    {}
};

template <typename T>
T value_or_throw(const Result<T>& result)
{
    if (result.has_value())
        return result.value();
    throw ParseError(result.error());
}

/**
 * @}
 */

namespace Details {
template <typename T>
struct StructTraits
{};
} // namespace Details

template <typename T>
Result<typename Details::StructTraits<T>::Type> parse_struct(const YamlAdapter::NodeRef& node);

template <typename T>
YamlAdapter::NodeRef serialize_struct(const typename Details::StructTraits<T>::Type& val);

template <typename T>
using LoaderFunc = std::function<void(T&&)>;

namespace Details {
inline std::string node_type_value(const NodeType type)
{
    switch (type) {
    case NodeType::Scalar:
        return "scalar";
    case NodeType::Sequence:
        return "sequence";
    case NodeType::Mapping:
        return "mapping";
    }
    return "unknown";
}

#define YAML_HANDLE_ENSURE(opt) if ((opt).has_value()) return ResultError{(opt).value()};

inline std::optional<ParseErrorDesc> ensure_node_not_null(const YamlAdapter::NodeRef& node)
{
    if (node.is_null())
        return ParseErrorDesc(node, "Null is not allowed here");
    return std::nullopt;
}

inline std::optional<ParseErrorDesc> ensure_node_type(const YamlAdapter::NodeRef& node, NodeType type)
{
    auto node_type = YamlAdapter::node_type(node);
    if (node_type != type)
        return ParseErrorDesc(
            node,
            std::string("Node type mismatch, expecting '")
                + node_type_value(type)
                + "' but got '"
                + node_type_value(node_type)
                + "'"
        );
    return std::nullopt;
}

inline Result<std::string_view> get_node_scalar(const YamlAdapter::NodeRef& node)
{
    YAML_HANDLE_ENSURE(ensure_node_type(node, NodeType::Scalar));
    YAML_HANDLE_ENSURE(ensure_node_not_null(node));
    return YamlAdapter::scalar_value(node);
}

template <typename T, typename Enabled = void>
struct TypeTraits
{};

template <>
struct TypeTraits<bool>
{
    static Result<bool> parse(const YamlAdapter::NodeRef& node) noexcept
    {
        auto value = get_node_scalar(node);
        if (value.has_value()) {
            if (*value == "true")
                return true;
            if (*value == "false")
                return false;
        } else
            return ResultError{value.error()};

        return ResultError{
            {node,
             fmt::format("Invalid bool value: '{}', allowed values are 'true' and 'false'", *value)}
        };
    }

    static std::optional<YamlAdapter::NodeRef> serialize(bool val)
    {
        std::string value = val ? "true" : "false";
        return YamlAdapter::create_scalar_node(value);
    }
};

template <typename T>
Result<T> parse_with_from_chars(const YamlAdapter::NodeRef& node)
{
    auto value = get_node_scalar(node);
    if (!value.has_value())
        return ResultError{value.error()};
    T ret;
    if constexpr (std::is_floating_point_v<T>) {
        auto r = fast_float::from_chars(value->data(), value->data() + value->size(), ret);
        if (r.ec != std::errc{} || r.ptr != value->data() + value->size())
            return ResultError{{node, fmt::format("Invalid {} value: '{}'", typeid(T).name(), *value)}};
    } else {
        auto [pend, ec] = std::from_chars(value->data(), value->data() + value->size(), ret);
        if (ec != std::errc{} || pend != value->data() + value->size())
            return ResultError{{node, fmt::format("Invalid {} value: '{}'", typeid(T).name(), *value)}};
    }
    return ret;
}

template <typename T>
YamlAdapter::NodeRef serialize_via_to_string(T val)
{
    return YamlAdapter::create_scalar_node(std::to_string(val));
}

#define TYPE_TRAITS_WITH_FROM_CHARS_PARSE(T)                    \
template <>                                                     \
struct TypeTraits<T>                                            \
{                                                               \
    static Result<T> parse(const YamlAdapter::NodeRef& node)    \
    {                                                           \
        return parse_with_from_chars<T>(node);                  \
    }                                                           \
    static std::optional<YamlAdapter::NodeRef> serialize(T val) \
    {                                                           \
        return serialize_via_to_string(val);                    \
    }                                                           \
};

TYPE_TRAITS_WITH_FROM_CHARS_PARSE(float)
TYPE_TRAITS_WITH_FROM_CHARS_PARSE(double)
TYPE_TRAITS_WITH_FROM_CHARS_PARSE(uint8_t)
TYPE_TRAITS_WITH_FROM_CHARS_PARSE(uint16_t)
TYPE_TRAITS_WITH_FROM_CHARS_PARSE(uint32_t)
TYPE_TRAITS_WITH_FROM_CHARS_PARSE(uint64_t)
TYPE_TRAITS_WITH_FROM_CHARS_PARSE(int8_t)
TYPE_TRAITS_WITH_FROM_CHARS_PARSE(int16_t)
TYPE_TRAITS_WITH_FROM_CHARS_PARSE(int32_t)

#undef TYPE_TRAITS_WITH_FROM_CHARS_PARSE

template <>
struct TypeTraits<std::string>
{
    static Result<std::string> parse(const YamlAdapter::NodeRef& node)
    {
        auto value = get_node_scalar(node);
        return value.transform([](auto&& v) { return std::string(v); });
    }

    static std::optional<YamlAdapter::NodeRef> serialize(const std::string& val)
    {
        return YamlAdapter::create_scalar_node(val);
    }
};

template <typename, typename = void>
struct HasTypeTraits : std::false_type
{};

template <typename T>
struct HasTypeTraits<
    T,
    std::void_t<decltype(TypeTraits<T>::parse(std::declval<const YamlAdapter::NodeRef&>()))>> :
    std::true_type
{};

template <typename T>
struct TypeTraits<std::optional<T>, std::enable_if_t<HasTypeTraits<T>::value>>
{
    static Result<std::optional<T>> parse(const YamlAdapter::NodeRef& node)
    {
        if (node && !node.is_null()) {
            auto val = TypeTraits<T>::parse(node);
            if (val.has_value())
                return *val;

            return ResultError{val.error()};
        }
        return std::nullopt;
    }

    static std::optional<YamlAdapter::NodeRef> serialize(const std::optional<T>& val)
    {
        if (val.has_value())
            return TypeTraits<T>::serialize(val.value());
        return std::nullopt;
    }
};

template <typename T>
struct TypeTraits<std::vector<T>, std::enable_if_t<HasTypeTraits<T>::value>>
{
    static Result<std::vector<T>> parse(const YamlAdapter::NodeRef& node)
    {
        YAML_HANDLE_ENSURE(ensure_node_type(node, NodeType::Sequence));
        std::vector<T> ret;
        ret.reserve(YamlAdapter::sequence_item_count(node));
        std::optional<ParseErrorDesc> parse_error;
        YamlAdapter::for_each_sequence_item(node, [&](const YamlAdapter::NodeRef& item) {
            if (parse_error) return;
            Result<T> element = TypeTraits<T>::parse(item);
            if (!element.has_value()) { parse_error.emplace(element.error()); return; }
            ret.push_back(std::move(*element));
        });
        if (parse_error) return ResultError{std::move(*parse_error)};
        return ret;
    }

    static std::optional<YamlAdapter::NodeRef> serialize(const std::vector<T>& val)
    {
        auto node = YamlAdapter::create_sequence_node();
        for (const auto& v : val) {
            auto element = TypeTraits<T>::serialize(v);
            ASSERT(element.has_value());
            YamlAdapter::sequence_append(node, element.value());
        }
        return node;
    }
};

template <typename K, typename V>
struct TypeTraits<std::map<K, V>, std::enable_if_t<HasTypeTraits<K>::value && HasTypeTraits<V>::value>>
{
    static Result<std::map<K, V>> parse(const YamlAdapter::NodeRef& node)
    {
        YAML_HANDLE_ENSURE(ensure_node_type(node, NodeType::Mapping));
        std::map<K, V> ret;
        std::optional<ParseErrorDesc> parse_error;
        YamlAdapter::for_each_mapping_item(
            node,
            [&](const YamlAdapter::KeyValuePair& kv_pair)
            {
                if (parse_error)
                    return;
                auto key_node   = YamlAdapter::key(kv_pair, node);
                auto value_node = YamlAdapter::value(kv_pair, node);
                Result<K> key   = TypeTraits<K>::parse(key_node);
                if (!key.has_value()) {
                    parse_error.emplace(key.error());
                    return;
                }
                Result<V> value = TypeTraits<V>::parse(value_node);
                if (!value.has_value()) {
                    parse_error.emplace(value.error());
                    return;
                }
                ret.emplace(std::move(*key), std::move(*value));
            }
        );
        if (parse_error) return ResultError{std::move(*parse_error)};
        return ret;
    }

    static std::optional<YamlAdapter::NodeRef> serialize(const std::map<K, V>& val)
    {
        auto node = YamlAdapter::create_mapping_node();
        for (const auto& [k, v] : val) {
            auto kn = TypeTraits<K>::serialize(k);
            auto vn = TypeTraits<V>::serialize(v);
            ASSERT(kn.has_value() && vn.has_value());
            YamlAdapter::mapping_append(node, kn.value(), vn.value());
        }
        return node;
    }
};

// unordered_map: O(1) average insert instead of O(log N) tree insert.
// Serialises with sorted keys so YAML output remains deterministic (important
// for diffs of user presets).
template <typename K, typename V>
struct TypeTraits<std::unordered_map<K, V>, std::enable_if_t<HasTypeTraits<K>::value && HasTypeTraits<V>::value>>
{
    static Result<std::unordered_map<K, V>> parse(const YamlAdapter::NodeRef& node)
    {
        YAML_HANDLE_ENSURE(ensure_node_type(node, NodeType::Mapping));
        std::unordered_map<K, V> ret;
        ret.reserve(YamlAdapter::mapping_item_count(node));
        std::optional<ParseErrorDesc> parse_error;
        YamlAdapter::for_each_mapping_item(node, [&](const YamlAdapter::KeyValuePair& kv_pair) {
            if (parse_error) return;
            auto key_node   = YamlAdapter::key(kv_pair, node);
            auto value_node = YamlAdapter::value(kv_pair, node);
            Result<K> key = TypeTraits<K>::parse(key_node);
            if (!key.has_value()) { parse_error.emplace(key.error()); return; }
            Result<V> value = TypeTraits<V>::parse(value_node);
            if (!value.has_value()) { parse_error.emplace(value.error()); return; }
            ret.emplace(std::move(*key), std::move(*value));
        });
        if (parse_error) return ResultError{std::move(*parse_error)};
        return ret;
    }

    static std::optional<YamlAdapter::NodeRef> serialize(const std::unordered_map<K, V>& val)
    {
        // Collect and sort keys so YAML output is deterministic across runs.
        std::vector<const K*> keys;
        keys.reserve(val.size());
        for (const auto& [k, _] : val)
            keys.push_back(&k);
        std::sort(keys.begin(), keys.end(), [](const K* a, const K* b) { return *a < *b; });

        auto node = YamlAdapter::create_mapping_node();
        for (const K* k : keys) {
            auto kn = TypeTraits<K>::serialize(*k);
            auto vn = TypeTraits<V>::serialize(val.at(*k));
            ASSERT(kn.has_value() && vn.has_value());
            YamlAdapter::mapping_append(node, kn.value(), vn.value());
        }
        return node;
    }
};

template <typename V, typename T>
Result<V> parse_variant(const YamlAdapter::NodeRef& node)
{
    return TypeTraits<T>::parse(node);
}

template <typename V, typename T, typename... Ts, std::enable_if_t<(sizeof...(Ts) > 0), int> = 0>
Result<V> parse_variant(const YamlAdapter::NodeRef& node)
{
    Result<T> val = TypeTraits<T>::parse(node);
    if (val.has_value())
        return val;
    return parse_variant<V, Ts...>(node);
}

template <typename... Ts>
struct AllHasTypeTraits : std::bool_constant<(HasTypeTraits<Ts>::value && ...)>
{};

template <typename... Ts>
struct TypeTraits<std::variant<Ts...>, std::enable_if_t<AllHasTypeTraits<Ts...>::value>>
{
    using ValueType = std::variant<Ts...>;

    static Result<ValueType> parse(const YamlAdapter::NodeRef& node)
    {
        return parse_variant<ValueType, Ts...>(node);
    }

    static std::optional<YamlAdapter::NodeRef> serialize(const std::variant<Ts...>& val)
    {
        return std::visit(
            []<typename T0>(const T0& v) { return TypeTraits<std::decay_t<T0>>::serialize(v); },
            val
        );
    }
};

template <>
struct TypeTraits<std::monostate>
{
    using ValueType = std::monostate;

    static Result<ValueType> parse(const YamlAdapter::NodeRef& node)
    {
        if (node.is_null())
            return {};
        return ResultError{ParseErrorDesc(node, "Node must be null")};
    }

    static std::optional<YamlAdapter::NodeRef> serialize(const std::monostate&)
    {
        return YamlAdapter::create_null_node();
    }
};

template <typename F, typename = void>
struct FieldHasImplicitValue : std::false_type
{};

template <typename F>
struct FieldHasImplicitValue<F, std::void_t<decltype(F::implicit_value())>> : std::true_type
{};

template <typename T>
struct IsOptional : std::false_type
{};

template <typename T>
struct IsOptional<std::optional<T>> : std::true_type
{};

template <typename F>
struct FieldIsOptional : IsOptional<typename F::Type>
{};

template <typename F, typename = void>
struct FieldHasValidation : std::false_type
{};

template <typename F>
struct FieldHasValidation<F, std::void_t<decltype(F::validate(std::declval<typename F::Type>()))>> :
    std::true_type
{};

template <typename... Ts>
struct TypeList
{};

template <typename Field, std::enable_if_t<!FieldHasImplicitValue<Field>::value, int> = 0>
Result<typename Field::Type> parse_field(const YamlAdapter::NodeRef& node)
{
    return TypeTraits<typename Field::Type>::parse(node);
}

template <typename Field, std::enable_if_t<FieldHasImplicitValue<Field>::value, int> = 0>
Result<typename Field::Type> parse_field(const YamlAdapter::NodeRef& node)
{
    if (!node)
        return Field::implicit_value();
    else
        return TypeTraits<typename Field::Type>::parse(node);
}

template <typename Field, typename = void>
void validate_field(const typename Field::Type& type)
{}

template <typename Field, std::enable_if_t<FieldHasValidation<Field>::value>>
void validate_field(const typename Field::Type& field_value)
{
    Field::validate(field_value);
}

template <typename S, typename Field, typename = void>
std::optional<ParseErrorDesc> parse_field(S& s, const YamlAdapter::NodeRef& node)
{
    using FT       = typename Field::Type;
    FT& typed_storage = s.*Field::member_ptr;
    auto val          = parse_field<Field>(node);
    if (!val.has_value())
        return val.error();
    typed_storage = std::move(val.value());
    validate_field<Field>(typed_storage);
    return std::nullopt;
}

template <
    typename Field,
    std::enable_if_t<!(FieldHasImplicitValue<Field>::value || FieldIsOptional<Field>::value), int> = 0>
Result<YamlAdapter::NodeRef> get_mapping_node_with_key(const YamlAdapter::NodeRef& node, const char* key)
{
    auto value_node = YamlAdapter::mapping_value_at(node, key);
    if (!value_node)
        return ResultError{ParseErrorDesc(node, fmt::format("Required field '{}' not found", key))};
    return value_node;
}

template <
    typename Field,
    std::enable_if_t<FieldHasImplicitValue<Field>::value || FieldIsOptional<Field>::value, int> = 0>
Result<YamlAdapter::NodeRef> get_mapping_node_with_key(const YamlAdapter::NodeRef& node, const char* key)
{
    return YamlAdapter::mapping_value_at(node, key);
}

template <typename T, typename F>
struct FieldTypeListHelper;

template <typename S, typename... Fs>
struct FieldTypeListHelper<S, TypeList<Fs...>>
{
    static std::optional<ParseErrorDesc> parse(S& s, const YamlAdapter::NodeRef& node)
    {
        if constexpr (sizeof...(Fs) == 0) {
            return std::nullopt;
        } else {
            return parse_fields<Fs...>(s, node);
        }
    }

    static void serialize(YamlAdapter::NodeRef& node, const S& s)
    {
        serialize_fields<Fs...>(node, s);
    }

private:
    template <typename F, typename... Rest>
    static std::optional<ParseErrorDesc> parse_fields(S& s, const YamlAdapter::NodeRef& node)
    {
        if constexpr (F::name == nullptr)
            return parse_fields_node<F, Rest...>(s, node, node);
        else {
            auto n = get_mapping_node_with_key<F>(node, F::name);
            if (!n.has_value())
                return n.error();
            return parse_fields_node<F, Rest...>(s, node, *n);
        }
    }

    template <typename F, typename... Rest>
    static std::optional<ParseErrorDesc> parse_fields_node(
        S& s,
        const YamlAdapter::NodeRef& node,
        const YamlAdapter::NodeRef& selected_node
    )
    {
        if (auto err = parse_field<S, F>(s, selected_node)) {
            return err;
        }
        if constexpr (sizeof...(Rest) > 0) {
            return parse_fields<Rest...>(s, node);
        }
        return std::nullopt;
    }

    template <typename F, typename... Rest>
    static void serialize_fields(YamlAdapter::NodeRef& node, const S& s)
    {
        if (F::name != nullptr) {
            auto key = YamlAdapter::create_scalar_node(F::name);

            using FT                  = typename F::Type;
            const FT& typed_storage = s.*F::member_ptr;

            if (!F::has_implicit_value(typed_storage)) {
                auto value = TypeTraits<FT>::serialize(typed_storage);
                if (value.has_value())
                    YamlAdapter::mapping_append(node, key, value.value());
            }
        }
        if constexpr (sizeof...(Rest) > 0) {
            serialize_fields<Rest...>(node, s);
        }
    }
};

template <typename S>
Result<typename S::Type> parse_struct_helper(const YamlAdapter::NodeRef& node)
{
    typename S::Type ret;

    auto opt_err = FieldTypeListHelper<typename S::Type, typename S::Fields>::parse(ret, node);

    if (opt_err.has_value())
        return ResultError{opt_err.value()};

    return ret;
}

template <typename T>
Result<bool> try_parse_discriminated_struct(
    const YamlAdapter::NodeRef& node,
    std::string_view value,
    std::tuple<const char*, LoaderFunc<T>> loader
)
{
    if (value == std::get<0>(loader)) {
        auto loader_load = std::get<1>(loader);

        // allow skipping sections by passing nullptr loader
        // so only some needed gets parsed
        if (loader_load != nullptr) {
            Result<T> s = parse_struct<T>(node);
            if (!s.has_value()) {
                return unexpected{s.error()};
            }
            loader_load(std::move(*s));
        }
        return true;
    }
    return false;
}

template <typename, typename = void>
struct HasStructTraits : std::false_type
{};

template <typename T>
struct HasStructTraits<T, std::void_t<typename StructTraits<T>::Type>> : std::true_type
{};

template <typename T>
struct TypeTraits<T, std::enable_if_t<HasStructTraits<T>::value>>
{
    static Result<T> parse(const YamlAdapter::NodeRef& node)
    {
        return parse_struct<T>(node);
    }

    static std::optional<YamlAdapter::NodeRef> serialize(const T& val)
    {
        return serialize_struct<T>(val);
    }
};

template <typename E, typename = void>
struct EnumTraits
{};

template <typename E>
struct EnumValue
{
    const char* name;
    const E value;

    constexpr EnumValue(const char* name, E value) noexcept : name(name), value(value) {}
};

template <typename, typename = void>
struct HasEnumTraits : std::false_type
{};

template <typename T>
struct HasEnumTraits<T, std::void_t<typename EnumTraits<T>::Type>> : std::true_type
{};

template <typename T>
struct TypeTraits<T, std::enable_if_t<std::is_enum_v<T> && !HasEnumTraits<T>::value>>
{
    static Result<T> parse(const YamlAdapter::NodeRef& node)
    {
        auto value = get_node_scalar(node);
        if (!value.has_value())
            return ResultError{value.error()};
        auto ret = magic_enum::enum_cast<T>(*value, magic_enum::case_insensitive);
        if (!ret.has_value()) {
            auto allowed_values = magic_enum::enum_names<T>();
            return ResultError{ParseErrorDesc{
                node,
                fmt::format(
                    "Invalid enum value: '{}', allowed values: {}",
                    *value,
                    fmt::join(allowed_values.begin(), allowed_values.end(), ", ")
                )
            }};
        }
        return ret.value();
    }

    static std::optional<YamlAdapter::NodeRef> serialize(const T& val)
    {
        return TypeTraits<std::underlying_type_t<T>>::serialize(val);
    }
};

template <typename T>
struct TypeTraits<T, std::enable_if_t<HasEnumTraits<T>::value>>
{
    static Result<T> parse(const YamlAdapter::NodeRef& node)
    {
        auto value         = TypeTraits<std::string>::parse(node);
        const auto& values = EnumTraits<T>::values;
        auto it            = std::find_if(values.begin(), values.end(), [&](const auto& v) {
            return v.name == value;
        });
        if (it == values.end()) {
            auto keys = values | std::views::transform([](const EnumValue<T>& ev) -> const char* {
                return ev.name;
            });
            return ResultError{ParseErrorDesc{
                node,
                fmt::format("Invalid enum value: '{}', allowed values: {}", *value, fmt::join(keys, ","))
            }};
        }
        return it->value;
    }

    static std::optional<YamlAdapter::NodeRef> serialize(const T& value)
    {
        const auto& values = EnumTraits<T>::values;
        auto it            = std::find_if(
            values.begin(),
            values.end(),
            [&](const auto& v) { return v.value == value; }
        );
        ASSERT(it != values.end());
        return YamlAdapter::create_scalar_node(it->name);
    }
};

} // namespace Details

/**
 * @name POD structures parsing API
 * @{
 */

/**
 * @brief Parse structure from yaml node
 * @tparam T Type of structure. Note that this structure needs STRUCT_DESC(...) definition.
 * @param node Yaml node representing the structure to load.
 * @return Loaded structure or ParseErrorDesc in `tl::expected`.
 */
template <typename T>
Result<typename Details::StructTraits<T>::Type> parse_struct(const YamlAdapter::NodeRef& node)
{
    return Details::parse_struct_helper<Details::StructTraits<T>>(node);
}

template <typename T>
YamlAdapter::NodeRef serialize_struct(const typename Details::StructTraits<T>::Type& val)
{
    YamlAdapter::NodeRef node = YamlAdapter::create_mapping_node();
    Details::FieldTypeListHelper<T, typename Details::StructTraits<T>::Fields>::serialize(
        node,
        val
    );
    return node;
}


/**
 * @brief Parse structure from yaml node
 * @tparam T Type of structure. Note that this structure needs STRUCT_DESC(...) definition.
 * @param doc Yaml YamlAdapter::Document containing the structure to load.
 * @return Loaded structure or ParseErrorDesc in `tl::expected`.
 */
template <typename T>
Result<typename Details::StructTraits<T>::Type> parse_struct(const YamlAdapter::Document& doc)
{
    return parse_struct<T>(doc.root());
}

/**
 * @brief Parse structure from yaml node
 * @tparam T Type of structure. Note that this structure needs STRUCT_DESC(...) definition.
 * @param doc Yaml YamlAdapter::Document containing the structure to load.
 * @return Loaded structure or ParseErrorDesc in `tl::expected`.
 */
template <typename T>
typename Details::StructTraits<T>::Type parse_struct_unwrap(const YamlAdapter::Document& doc)
{
    auto ret = parse_struct<T>(doc);
    if (!ret.has_value()) {
        throw ParseError(ret.error());
    }
    return ret.value();
}

/**
 * @brief Load struct from possible set of types distinguished by value in discriminator field.
 * @tparam Ts types of structs to load
 * @param node Yaml node containing structure to load and the discriminator_field_name
 * @param discriminator_field_name A name of field the type of structure is distinguished by
 * @param loaders List (variadic args) of two element tuples containing `const char*` value of
 * discriminator and function that will be called with loaded struct of given type.
 */
template <typename... Ts>
void parse_structs_by_discriminant(
    const YamlAdapter::NodeRef& node,
    const char* discriminator_field_name,
    const std::tuple<const char*, LoaderFunc<Ts>>&... loaders
)
{
    auto discr_node  = YamlAdapter::mapping_value_at(node, discriminator_field_name);
    auto discr_value = Details::get_node_scalar(discr_node);

    if (!discr_value.has_value())
        throw ParseError(discr_value.error());

    if (!(value_or_throw(Details::try_parse_discriminated_struct(node, *discr_value, loaders)) || ...))
    {
        std::vector<const char*> discr_field_values;
        (discr_field_values.push_back(std::get<0>(loaders)), ...);
        throw ParseError(
            node,
            fmt::format(
                "Cannot parse any structure identified by field (discriminator) '{}' with value '{}'. Allowed values are: {}",
                discriminator_field_name,
                *discr_value,
                fmt::join(discr_field_values.begin(), discr_field_values.end(), ", ")
            )
        );
    }
}

/**
 * @brief Load struct from possible set of types distinguished by value in discriminator field.
 * @tparam Ts types of structs to load
 * @param doc Yaml YamlAdapter::Document containing structure to load and the discriminator_field_name
 * @param discriminator_field_name A name of field the type of structure is distinguished by
 * @param loaders List (variadic args) of two element tuples containing `const char*` value of
 * discriminator and function that will be called with loaded struct of given type.
 */
template <typename... Ts>
void parse_structs_by_discriminant(
    const YamlAdapter::Document& doc,
    const char* discriminator_field_name,
    const std::tuple<const char*, LoaderFunc<Ts>>&... loaders
)
{
    parse_discriminated_structs(doc.root(), discriminator_field_name, loaders...);
}

template <typename T>
std::string write_string(const T& val)
{
    auto node = Details::TypeTraits<T>::serialize(val);
    ASSERT(node.has_value());
    auto emitter = YamlAdapter::create_emitter(node.value());
    auto data = YamlAdapter::emitter_output(emitter);
    return std::string{data};
}


template <typename T>
void write_file(const T& val, const char* filename)
{
    auto data = write_string(val);
    boost::nowide::ofstream file(filename, std::ios::binary | std::ios::out);
    if (!file.good()) {
        throw SerializationError(fmt::format("Cannot write file {}", filename), filename);
    }
    file.write(data.data(), data.size());
    file.close();

}

namespace Details {
template <typename T>
struct ImplicitValueHelper
{
    static constexpr bool is_implicit_value(const T& impl_val, const T& val)
    {
        return false;
    }
};

template <Domain::DeepEquality T>
struct ImplicitValueHelper<T>
{
    static constexpr bool is_implicit_value(const T& impl_val, const T& val)
    {
        return impl_val == val;
    }
};

template <typename T, typename A>
struct ImplicitValueHelper<std::vector<T, A>>
{
    using Type = std::vector<T, A>;

    static constexpr bool is_implicit_value(const Type& impl_val, const Type& val)
    {
        return impl_val.empty() && val.empty();
    }
};

template <typename K, typename V, typename A>
struct ImplicitValueHelper<std::map<K, V, A>>
{
    using Type = std::map<K, V, A>;

    static constexpr bool is_implicit_value(const Type& impl_val, const Type& val)
    {
        return impl_val.empty() && val.empty();
    }
};

} // namespace Details
} // namespace Slic3r::Biz::Yaml

// For each field, generate a struct with type, name, and offset.
#define DETAILS_STRUCT_DESC_FIELD(r, data, elem)                                                \
struct BOOST_PP_CAT(Field_, BOOST_PP_TUPLE_ELEM(0, elem)) {                                     \
    using Type = decltype(std::declval<data>().BOOST_PP_TUPLE_ELEM(0, elem));                   \
    static constexpr const char* name = BOOST_PP_IF(                                            \
        BOOST_PP_IS_EMPTY(BOOST_PP_TUPLE_ELEM(1, elem)),                                        \
        BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(0, elem)),                                       \
        BOOST_PP_TUPLE_ELEM(1, elem)                                                            \
    );                                                                                          \
    static constexpr auto member_ptr = &data::BOOST_PP_TUPLE_ELEM(0,elem);                       \
    BOOST_PP_IF(                                                                                \
        BOOST_PP_NOT(BOOST_PP_IS_EMPTY(BOOST_PP_TUPLE_ELEM(2, elem))),                          \
        static Type implicit_value() { return BOOST_PP_TUPLE_ELEM(2, elem); }                   \
        static bool has_implicit_value(const Type& val)                                         \
        {                                                                                       \
            using namespace ::Slic3r::Biz::Yaml::Details;                                       \
            return ImplicitValueHelper<Type>::is_implicit_value(implicit_value(), val);         \
        }                                                                                       \
        ,                                                                                       \
        static constexpr bool has_implicit_value(const Type&) { return false; }                 \
    )                                                                                           \
};

// This helper macro transforms a field name into its corresponding Field_ struct name.
#define DETAILS_STRUCT_DESC_FIELD_NAME_OP(r, data, elem) \
    BOOST_PP_CAT(Field_, BOOST_PP_TUPLE_ELEM(0, elem))

// Define struct description
// Struct - type
// __VA_ARGS__ - field description tuple (field, opt_name, opt_impl_value, opt_validation)
#define STRUCT_DESC(Struct, ...)                                                                   \
namespace Slic3r::Biz::Yaml::Details {                                                             \
template <>                                                                                        \
struct StructTraits<Struct> {                                                                      \
    BOOST_PP_SEQ_FOR_EACH(DETAILS_STRUCT_DESC_FIELD, Struct, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))\
    using Fields = ::Slic3r::Biz::Yaml::Details::TypeList<                                         \
        BOOST_PP_SEQ_ENUM(                                                                         \
            BOOST_PP_SEQ_TRANSFORM(                                                                \
                DETAILS_STRUCT_DESC_FIELD_NAME_OP,                                                 \
                ~,                                                                                 \
                BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)                                              \
            )                                                                                      \
        )                                                                                          \
    >;                                                                                             \
    using Type = Struct;                                                                           \
};                                                                                                 \
} // namespace  Yaml::Details

#define FIELD_DESC_SIMPLE(field) (field, #field,,)

#define DETAILS_TRANSFORM_FIELD_SIMPLE(r, data, elem) FIELD_DESC_SIMPLE(elem)
#define STRUCT_DESC_SIMPLE(Struct, ...)                                 \
    STRUCT_DESC(Struct,                                                 \
        BOOST_PP_SEQ_ENUM(                                              \
            BOOST_PP_SEQ_TRANSFORM(                                     \
                DETAILS_TRANSFORM_FIELD_SIMPLE,                         \
                ~,                                                      \
                BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)                   \
            )                                                           \
        )                                                               \
    )

#define FIELD_DEFAULT
#define FIELD_NAME_SELF nullptr
#define FIELD_DESC(field, opt_field_name, opt_implicit_value, opt_validation) \
    (field, opt_field_name, opt_implicit_value, opt_validation)
#define FIELD_DESC_IMPLICIT_VALUE(field, implicit_value) \
        (field, FIELD_DEFAULT, implicit_value, FIELD_DEFAULT)

#define DETAILS_ENUM_VALUE_IF(r, data, elem)                \
    if (value == BOOST_PP_TUPLE_ELEM(0, elem))              \
        return data::BOOST_PP_TUPLE_ELEM(1, elem);
#define DETAILS_ENUM_VALUE_GET(r, data, elem)               \
    EnumValue(BOOST_PP_TUPLE_ELEM(0, elem), data::BOOST_PP_TUPLE_ELEM(1, elem))

#define ENUM_DESC(Enum, ...)                                                                    \
namespace Slic3r::Biz::Yaml::Details {                                                          \
template <>                                                                                     \
struct EnumTraits<Enum>                                                                         \
{                                                                                               \
    using Type = Enum;                                                                          \
    static constexpr std::optional<Type> parse(const std::string& value)                        \
    {                                                                                           \
        BOOST_PP_SEQ_FOR_EACH(                                                                  \
            DETAILS_ENUM_VALUE_IF,                                                              \
            Enum,                                                                               \
            BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)                                               \
        )                                                                                       \
        return std::nullopt;                                                                    \
    }                                                                                           \
    using NameValuePair = EnumValue<Type>;                                                      \
    static constexpr std::array<NameValuePair, BOOST_PP_VARIADIC_SIZE(__VA_ARGS__)> values = {  \
        BOOST_PP_SEQ_ENUM(                                                                      \
            BOOST_PP_SEQ_TRANSFORM(                                                             \
                DETAILS_ENUM_VALUE_GET,                                                         \
                Enum,                                                                           \
                BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)                                           \
            )                                                                                   \
        )                                                                                       \
    };                                                                                          \
};                                                                                              \
} //namespace Slic3r::Biz::Yaml::Details
