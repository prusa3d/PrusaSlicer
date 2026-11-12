#include "Slic3r/Biz/Yaml/Yaml.hpp"

namespace Slic3r::Biz::Yaml {

void parse_all_documents(
    const YamlAdapter::Parser& parser,
    const std::function<void(const YamlAdapter::Document&)>& parse_doc
)
{
    while (YamlAdapter::Document doc = YamlAdapter::load(parser)) {
        parse_doc(doc);
    }
}

YamlAdapter::Document parse_file(const char* file_name)
{
    YamlAdapter::Parser parser{YamlAdapter::create_file_parser(file_name)};
    return YamlAdapter::load(parser);
}

YamlAdapter::Document parse_string(std::string_view yaml)
{
    YamlAdapter::Parser parser{YamlAdapter::create_string_parser(yaml)};
    return YamlAdapter::load(parser);
}

void parse_all_documents_in_file(
    const char* file_name,
    const std::function<void(const YamlAdapter::Document&)>& parse_doc
)
{
    YamlAdapter::Parser parser{YamlAdapter::create_file_parser(file_name)};
    parse_all_documents(parser, parse_doc);
}

void parse_all_documents_in_string(
    std::string_view yaml,
    const std::function<void(const YamlAdapter::Document&)>& parse_doc
)
{
    YamlAdapter::Parser parser{YamlAdapter::create_string_parser(yaml)};
    parse_all_documents(parser, parse_doc);
}

} // namespace Slic3r::Biz::Yaml
