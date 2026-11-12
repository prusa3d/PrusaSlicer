#include "Slic3r/Biz/Preset/IO/PresetSaver.hpp"
#include "Slic3r/Biz/Preset/IO/BundlePaths.hpp"
#include "Slic3r/Biz/Yaml/Yaml.hpp"
#include "Slic3r/Biz/Yaml/YamlSlic3rTypes.hpp"
#include "Slic3r/Biz/Preset/IO/PresetYamlDesc.hpp"
#include "Slic3r/Biz/Expr/Parser.hpp"

#include "boost/filesystem/operations.hpp"

namespace Slic3r::Biz::Preset::IO::Details {

Domain::Expr::ExprAst and_chain_exprs(const std::vector<std::string>& exprs)
{
    ASSERT(!exprs.empty());

    Domain::Expr::ExprAst ret = Biz::Expr::Parser().parse(exprs[0]);

    for (size_t i = 1; i < exprs.size(); ++i) {
        ret = Domain::Expr::Binary{Domain::Expr::BinaryOp::And, ret, Biz::Expr::Parser().parse(exprs[i])};
    }

    return ret;
}

std::string convert(bool v)
{
    return v ? "true" : "false";
}

std::string convert(const Domain::Vec2d& v)
{
    return fmt::format("{}x{}", v.x(), v.y());
}

std::string convert(const Domain::EnumWrapper& v)
{
    return std::string{v.get_string()};
}

Domain::Preset::Strings convert(const Domain::EnumVectorWrapper& v)
{
    Domain::Preset::Strings ret;
    auto source = v.get_strings();
    std::ranges::transform(
        source,
        std::back_inserter(ret),
        [](std::string_view vi) { return std::string(vi); }
    );
    return ret;
}

template <typename T>
concept HavingToString = requires(T t)
{
    { std::to_string(t) } -> std::same_as<std::string>;
};

std::string convert(const std::string& v) { return v; }

template <HavingToString T>
std::string convert(const T& v)
{
    return std::to_string(v);
}

template <typename  T>
Domain::Preset::PresetValue convert(const std::optional<T>& v);
template <typename  T>
Domain::Preset::PresetValue convert(const std::vector<T>& v);

template <typename T>
concept ConvertibleToPresetValue = requires(const T& t) {
    { convert(t) } -> std::convertible_to<Domain::Preset::PresetValue>;
};

template <typename T>
concept StorableToPresetValue = requires(T t)
{
    { t } -> std::convertible_to<Domain::Preset::PresetValue>;
};

template <typename T>
concept VectorStorableToPresetValue = requires(std::vector<T> t)
{
    { t } -> std::convertible_to<Domain::Preset::PresetValue>;
};

template <typename  T>
    //requires ConvertibleToPresetValue<T>
Domain::Preset::PresetValue convert(const std::optional<T>& v)
{
    static_assert(ConvertibleToPresetValue<T>);
    Domain::Preset::PresetValue ret;
    if (v.has_value()) {
        ret = convert(*v);
    } else {
        ret = std::monostate{};
    }
    return ret;
}

Domain::Preset::PresetValue convert(const std::vector<std::optional<int>>& v)
{
    return v;
}

Domain::Preset::PresetValue convert(const Domain::Preset::FloatOrPercentages& v)
{
    return v;
}


Domain::FloatOrPercentage convert(const Domain::FloatOrPercentage& v)
{
    return v;
}

Domain::Preset::FloatOrPercentages convert(const std::vector<Domain::Percentage>& v)
{
    Domain::Preset::FloatOrPercentages ret;
    std::ranges::transform(
        v,
        std::back_inserter(ret),
        [](const Domain::Percentage& p) { return Domain::FloatOrPercentage(p); }
    );
    return ret;
}

template <typename  T>
    //requires VectorStorableToPresetValue<T>
Domain::Preset::PresetValue convert(const std::vector<T>& v)
{
    static_assert(VectorStorableToPresetValue<T>);

    using ResultType = decltype(convert(std::declval<T>()));
    std::vector<ResultType> ret;
    std::transform(
        v.begin(),
        v.end(),
        std::back_inserter(ret),
        []<typename E>(const E& val)
        {
            return convert(val);
        }
    );
    return ret;
}

void append_selected_config_items(
    Domain::Preset::PresetValueMap& ret,
    const std::vector<Domain::ConfigItem>& all_items,
    const KeySet& items_to_include
)
{
    for (const auto& item : all_items) {
        if (!items_to_include.contains(item.name())) {
            continue;
        }

        ret.insert(
            {item.name(),
             item.visit(
                 []<typename T>(const T& val) -> Domain::Preset::PresetValue
                 {
                     if constexpr (std::is_assignable_v<Domain::Preset::PresetValue, T>) {
                         return val;
                     } else if constexpr (std::
                                              is_same_v<std::decay_t<T>, Domain::FloatOrPercentage>)
                     {
                         if (val.is_percentage())
                             return val.percentage();
                         return val.float_value();
                     } else {
                         return convert(val);
                     }
                 }
             )}
        );
    }
}

Domain::Preset::PresetValueMap
config_box_to_values(const Domain::ConfigBox& cfg, const KeySet& items_to_include)
{
    Domain::Preset::PresetValueMap ret;

    append_selected_config_items(ret, cfg.items.all_items(), items_to_include);
    append_selected_config_items(ret, cfg.overrides.all_items(), items_to_include);

    return ret;
}


} // namespace Slic3r::Biz::Preset::IO::Details

namespace Slic3r::Biz::Preset::IO {

void save_transformed_preset_as_user(const Domain::Preset::RootPresetNode& root_preset, const std::string& path)
{
    Yaml::write_file(root_preset, path.c_str());
}

std::string preset_file_prefix(Domain::Preset::PresetKind kind)
{
    std::string prefix;
    switch (kind) {
    case Domain::Preset::PresetKind::FdmPrinter:
        prefix = "printer";
        break;

    case Domain::Preset::PresetKind::SlaPrinter:
        prefix = "sla-printer";
        break;

    case Domain::Preset::PresetKind::FdmPrint:
        prefix = "print";
        break;

    case Domain::Preset::PresetKind::SlaPrint:
        prefix = "sla-print";
        break;

    case Domain::Preset::PresetKind::FdmToolPrint:
        prefix = "tool";
        break;

    case Domain::Preset::PresetKind::SlaToolPrint:
        prefix = "sla-tool";
        break;

    case Domain::Preset::PresetKind::FdmMaterial:
        prefix = "filament";
        break;

    case Domain::Preset::PresetKind::SlaMaterial:
        prefix = "material";
        break;
    }

    if (!prefix.empty()) {
        prefix += "-";
    }
    return prefix;
}

boost::filesystem::path preset_path(
    const BundlePaths& paths,
    Domain::Preset::PresetKind kind,
    const std::string& preset_name,
    const std::string& vendor_id,
    const std::string& repo_id
)
{
    namespace fs = boost::filesystem;

    auto dir_path = paths.user_preset_dir_path(vendor_id, repo_id);
    if (!fs::exists(dir_path))
        fs::create_directories(dir_path);

    std::string file_name = preset_file_prefix(kind) + preset_name + ".yaml";

    return dir_path / file_name;
}

}