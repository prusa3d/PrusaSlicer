#pragma once

#include "Slic3r/Domain/Percentage.hpp"
#include "Slic3r/Domain/Expr/ExprAst.hpp"
#include "Slic3r/Domain/Preset/PresetTree.hpp"
#include "Slic3r/Biz/Expr/Parser.hpp"
#include "Slic3r/Biz/Yaml/Yaml.hpp"
#include "Slic3r/Biz/Expr/Simplify.hpp"

namespace Slic3r::Biz::Yaml::Details {

template <>
struct TypeTraits<Slic3r::Domain::Expr::ExprAst>
{
    using ExprAst = Slic3r::Domain::Expr::ExprAst;
    using Parser  = Slic3r::Biz::Expr::Parser;

    static Result<ExprAst> parse(const YamlAdapter::NodeRef& node)
    {
        static Parser parser;
        try {
            auto node_value = get_node_scalar(node);
            if (!node_value.has_value())
                return ResultError(node_value.error());
            return parser.parse(*node_value);
        } catch (Slic3r::Biz::Expr::ParseError& e) {
            return ResultError(ParseErrorDesc(node, e.what()));
        }
    }

    static std::optional<YamlAdapter::NodeRef> serialize(const ExprAst& val)
    {
        return YamlAdapter::create_scalar_node(to_string(val));
    }

};

template <>
struct TypeTraits<Slic3r::Domain::Preset::SourceLocation>
{
    using SourceLocation = Slic3r::Domain::Preset::SourceLocation;

    static Result<SourceLocation> parse(const YamlAdapter::NodeRef& node)
    {
        auto mark = YamlAdapter::mark(node);
        return Slic3r::Domain::Preset::SourceLocation{std::string{mark.file}, mark.line, mark.column};
    }

    static std::optional<YamlAdapter::NodeRef> serialize(const SourceLocation&)
    {
        return std::nullopt;
    }
};

template <typename T>
struct TypeTraits<Slic3r::Domain::Preset::SourceLocated<T>>
{
    using SourceLocated  = Slic3r::Domain::Preset::SourceLocated<T>;
    using SourceLocation = Slic3r::Domain::Preset::SourceLocation;

    static Result<Slic3r::Domain::Preset::SourceLocated<T>> parse(const YamlAdapter::NodeRef& node)
    {
        auto data = TypeTraits<T>::parse(node);
        if (!data.has_value())
            return ResultError(data.error());

        auto source_location = TypeTraits<SourceLocation>::parse(node);
        if (!source_location.has_value())
            return ResultError(source_location.error());

        SourceLocated ret{*data, *source_location};
        return ret;
    }

    static std::optional<YamlAdapter::NodeRef> serialize(const SourceLocated& v)
    {
        return TypeTraits<T>::serialize(v.value);
    }
};

template <>
struct TypeTraits<Slic3r::Domain::Preset::ParsedExpr>
{
    using Expr          = Domain::Expr::ExprAst;
    using SourceLocated = Slic3r::Domain::Preset::SourceLocated<Domain::Expr::ExprAst>;

    static Result<Slic3r::Domain::Preset::ParsedExpr> parse(const YamlAdapter::NodeRef& node)
    {
        auto data = TypeTraits<SourceLocated>::parse(node);
        if (!data.has_value())
            return ResultError(data.error());

        data.value().value = Slic3r::Biz::Expr::simplify(data.value().value);

        std::string expr_str = Domain::Expr::to_string(data.value().value);

        return Slic3r::Domain::Preset::ParsedExpr{std::move(data.value()), std::move(expr_str)};
    }

    static std::optional<YamlAdapter::NodeRef> serialize(
        const Slic3r::Domain::Preset::ParsedExpr& v
    )
    {
        return TypeTraits<SourceLocated>::serialize(v.expr);
    }
};

template <>
struct TypeTraits<Slic3r::Domain::Vec2d>
{
    using Vec2d = Slic3r::Domain::Vec2d;

    static Result<Vec2d> parse(const YamlAdapter::NodeRef& node)
    {
        Vec2d ret;

        auto node_value = get_node_scalar(node);
        if (!node_value.has_value())
            return ResultError(node_value.error());
        auto value = *node_value;
        auto pos   = value.find('x');
        if (pos == std::string::npos)
            return ResultError(ParseErrorDesc(node, fmt::format("Invalid Vec2d value '{}'", value)));

        auto r1 = fast_float::from_chars(value.data(), value.data() + pos, ret.x());
        if (r1.ec != std::errc{} || r1.ptr != value.data() + pos)
            return ResultError(ParseErrorDesc(node, fmt::format("Invalid Vec2d value: '{}'", value)));
        auto r2 = fast_float::from_chars(value.data() + pos + 1, value.data() + value.size(), ret.y());
        if (r2.ec != std::errc{} || r2.ptr != value.data() + value.size())
            return ResultError(ParseErrorDesc(node, fmt::format("Invalid Vec2d value: '{}'", value)));

        return ret;
    }

    static std::optional<YamlAdapter::NodeRef> serialize(const Vec2d& val)
    {
        return YamlAdapter::create_scalar_node(fmt::format("{}x{}", val.x(), val.y()));
    }
};

template <>
struct TypeTraits<Slic3r::Domain::Percentage>
{
    using Percentage = Slic3r::Domain::Percentage;

    static Result<Percentage> parse(const YamlAdapter::NodeRef& node)
    {
        Percentage ret;

        auto node_value = get_node_scalar(node);
        if (!node_value.has_value())
            return ResultError(node_value.error());
        auto value = *node_value;
        auto pos   = value.find('%');
        bool valid = pos != std::string::npos
            && std::all_of(value.cbegin() + pos + 1, value.cend(), [](char c) {
            return std::isspace(c);
        });
        if (valid) {
            auto r = fast_float::from_chars(value.data(), value.data() + pos, ret.value);
            valid  = (r.ec == std::errc{} && r.ptr == value.data() + pos);
        }

        if (!valid)
            return ResultError(
                ParseErrorDesc(node, fmt::format("Invalid Percentage value '{}'", value))
            );

        return ret;
    }

    static std::optional<YamlAdapter::NodeRef> serialize(const Percentage& val)
    {
        return YamlAdapter::create_scalar_node(fmt::format("{}%", val.value));
    }
};

template <>
struct TypeTraits<Slic3r::Domain::FloatOrPercentage>
{
    using FloatOrPercentage = Domain::FloatOrPercentage;
    using Percentage = Domain::Percentage;

    static Result<FloatOrPercentage> parse(const YamlAdapter::NodeRef& node)
    {
        auto val = TypeTraits<Percentage>::parse(node);
        if (val.has_value())
            return val.value();
        return TypeTraits<double>::parse(node);
    }

    static std::optional<YamlAdapter::NodeRef> serialize(const FloatOrPercentage& val)
    {
        if (val.is_percentage())
            return TypeTraits<Percentage>::serialize(val.percentage());
        return TypeTraits<double>::serialize(val.float_value());
    }
};

template <>
struct TypeTraits<Domain::JsonObject>;

Result<Domain::JsonValue> parse_json_value(const YamlAdapter::NodeRef& node);
std::optional<YamlAdapter::NodeRef> serialize_json_value(const Domain::JsonValue& val);

template <>
struct TypeTraits<Domain::JsonArray>
{
    using JsonArray = Domain::JsonArray;
    static Result<JsonArray> parse(const YamlAdapter::NodeRef& node)
    {
        YAML_HANDLE_ENSURE(ensure_node_type(node, NodeType::Sequence));
        JsonArray ret;
        ret.reserve(YamlAdapter::sequence_item_count(node));
        std::optional<ParseErrorDesc> parse_error;
        YamlAdapter::for_each_sequence_item(node, [&](const YamlAdapter::NodeRef& item) {
            if (parse_error) return;
            auto parsed_value = parse_json_value(item);
            if (!parsed_value.has_value()) { parse_error.emplace(parsed_value.error()); return; }
            ret.push_back(std::move(parsed_value.value()));
        });
        if (parse_error) return ResultError{std::move(*parse_error)};
        return ret;
    }

    static std::optional<YamlAdapter::NodeRef> serialize(const JsonArray& val)
    {
        auto node = YamlAdapter::create_sequence_node();
        for (const auto& item : val) {
            auto item_node = serialize_json_value(item);
            if (!item_node.has_value())
                return std::nullopt;
            YamlAdapter::sequence_append(node, item_node.value());
        }
        return node;
    }
};

template <>
struct TypeTraits<Domain::JsonObject>
{
    using JsonObject = Domain::JsonObject;
    static Result<JsonObject> parse(const YamlAdapter::NodeRef& node)
    {
        YAML_HANDLE_ENSURE(ensure_node_type(node, NodeType::Mapping));
        JsonObject ret;
        std::optional<ParseErrorDesc> parse_error;
        YamlAdapter::for_each_mapping_item(node, [&](const YamlAdapter::KeyValuePair& kv_pair) {
            if (parse_error) return;
            auto parsed_value = parse_json_value(YamlAdapter::value(kv_pair, node));
            if (!parsed_value.has_value()) { parse_error.emplace(parsed_value.error()); return; }
            auto parsed_key = TypeTraits<std::string>::parse(YamlAdapter::key(kv_pair, node));
            if (!parsed_key.has_value()) { parse_error.emplace(parsed_key.error()); return; }
            ret.emplace(std::move(*parsed_key), std::move(parsed_value.value()));
        });
        if (parse_error) return ResultError{std::move(*parse_error)};
        return ret;
    }

    static std::optional<YamlAdapter::NodeRef> serialize(const JsonObject& val)
    {
        auto node = YamlAdapter::create_mapping_node();
        for (const auto& [k, v] : val) {
            auto value_node = serialize_json_value(v);
            if (!value_node.has_value())
                return std::nullopt;
            auto key_node = YamlAdapter::create_scalar_node(k);
            YamlAdapter::mapping_append(node, key_node, value_node.value());
        }
        return node;

    }
};

template <>
struct TypeTraits<Domain::JsonValue>
{
    static Result<Domain::JsonValue> parse(const YamlAdapter::NodeRef& node)
    {
        return parse_json_value(node);
    }

    static std::optional<YamlAdapter::NodeRef> serialize(const Domain::JsonValue& val)
    {
        return serialize_json_value(val);
    }
};


// ---------------------------------------------------------------------------
// PresetValue — node-type dispatch
//
// The generic parse_variant<PresetValue, all-14-types> tries alternatives left
// to right, creating (and discarding) a ParseErrorDesc on each mismatch.
// For a scalar string value that means 13 wasted constructions per entry.
//
// This specialisation checks the YAML node type first (one cheap call) and
// then only attempts the alternatives that can actually match:
//   Sequence node → try the seven vector types only
//   Scalar  node  → null → monostate; otherwise try the six scalar types
//   Mapping node  → error (no mapping alternative in PresetValue)
//
// Combined with lazy ParseErrorDesc, the remaining failed probes (≤5 for a
// scalar string) no longer pay the fmt::format cost either.
// ---------------------------------------------------------------------------
template <>
struct TypeTraits<Domain::Preset::PresetValue>
{
    using PresetValue = Domain::Preset::PresetValue;
    static_assert(std::variant_size_v<PresetValue> == 14,
        "PresetValue alternatives changed — update the Sequence/Scalar dispatch below");

    static Result<PresetValue> parse(const YamlAdapter::NodeRef& node)
    {
        if (node.is_null())
            return std::monostate{};

        switch (YamlAdapter::node_type(node)) {
        case NodeType::Sequence:
            return parse_variant<PresetValue,
                Domain::Preset::Bools,
                Domain::Preset::Doubles,
                Domain::Preset::Ints,
                Domain::Preset::OptInts,
                Domain::Preset::FloatOrPercentages,
                Domain::Vec2ds,
                Domain::Preset::Strings
            >(node);

        case NodeType::Scalar:
            return parse_variant<PresetValue,
                bool, double, int,
                Domain::Percentage,
                Domain::Vec2d,
                std::string
            >(node);

        case NodeType::Mapping:
            return ResultError{ParseErrorDesc{
                node, "preset value cannot be a YAML mapping"
            }};
        }
        return ResultError{ParseErrorDesc{node, "unknown node type"}};
    }

    static std::optional<YamlAdapter::NodeRef> serialize(const PresetValue& val)
    {
        return std::visit(
            []<typename T>(const T& v) -> std::optional<YamlAdapter::NodeRef> {
                return TypeTraits<std::decay_t<T>>::serialize(v);
            },
            val
        );
    }
};

} // namespace Slic3r::Biz::Yaml::Details
