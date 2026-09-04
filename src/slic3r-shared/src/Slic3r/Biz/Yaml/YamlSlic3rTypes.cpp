#include "Slic3r/Biz/Yaml/YamlSlic3rTypes.hpp"

#include "Slic3r/Domain/TemplateUtils.hpp"

namespace Slic3r::Biz::Yaml::Details {

Result<Domain::JsonValue> parse_json_value(const YamlAdapter::NodeRef& node)
{
    switch (YamlAdapter::node_type(node)) {
    case NodeType::Sequence:
        return TypeTraits<Domain::JsonArray>::parse(node);
    case NodeType::Mapping:
        return TypeTraits<Domain::JsonObject>::parse(node);
    case NodeType::Scalar:
        if (auto v = TypeTraits<bool>::parse(node); v.has_value())
            return v;
        if (auto v = TypeTraits<double>::parse(node); v.has_value())
            return v;
        return TypeTraits<std::string>::parse(node);
    }

    PANIC("Unknown YAML node type");
}

std::optional<YamlAdapter::NodeRef> serialize_json_value(const Domain::JsonValue& val)
{
    return std::visit(
        Domain::overloaded{
            [](const std::nullptr_t&) -> std::optional<YamlAdapter::NodeRef>
            { return YamlAdapter::create_null_node(); },
            []<typename T>(const T& v) { return TypeTraits<T>::serialize(v); }
        },
        val
    );
}

} // namespace Slic3r::Biz::Yaml::Details
