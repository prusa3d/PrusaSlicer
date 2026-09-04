#pragma once

// The point of this header file is to contain all cerealization templates
// which require cereal includes. This would ideally be only included
// in cpp files which actually need to serialize something.

// The usual 'void serialize(Archive&)' template methods can as well stay
// with the respective class so we don't split the object from its serialization
// recipe. Only when the cereal header is needed, it should be moved here.

#include "cereal/cereal.hpp"
#include <cereal/specialize.hpp>
#include <cereal/types/base_class.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/variant.hpp>

#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ConfigValue.hpp"
#include "Slic3r/Domain/Expr/ExprAst.hpp"
#include "Slic3r/Domain/EmbossShape.hpp"
#include "Slic3r/Domain/LayerHeightProfile.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/Percentage.hpp"
#include "Slic3r/Domain/Preset/Bundle.hpp"
#include "Slic3r/Domain/Preset/EvaluatedPreset.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/Preset/SourceLocatedExpr.hpp"
#include "Slic3r/Domain/Preset/PresetTree.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/TextConfiguration.hpp"


namespace cereal {
    template <class Archive>
    void serialize(Archive& ar, Slic3r::Domain::FontProp& prop)
    {
        ar(prop.size_in_mm, prop.per_glyph, prop.align);
        ar(prop.char_gap);
        ar(prop.line_gap);
        ar(prop.boldness);
        ar(prop.skew);
        ar(prop.collection_number);
    }

    template <class Archive>
    void serialize(Archive& ar, Slic3r::Domain::FontDescriptor& descriptor)
    {
        ar(descriptor.name, descriptor.path, descriptor.type);
    }

    template <class Archive>
    void serialize(Archive& ar, Slic3r::Domain::EmbossStyle& style)
    {
        ar(style.descriptor, style.prop);
    }

    template <class Archive, class T, int N>
    void serialize(Archive& archive, Slic3r::Domain::Advanced::Vec<T, N>& vector)
    {
        for (int i = 0; i < vector.size(); ++i) {
            archive(vector[i]);
        }
    }

    template<class Archive> void serialize(Archive& archive, Slic3r::Domain::SquareMatrix4d &m){ archive(binary_data(m.data(), 4*4*sizeof(double))); }
    template <class Archive, class T, int N>
    void serialize(Archive& archive, Slic3r::Domain::Advanced::Transform<T, N>& transform)
    {
        archive(transform.matrix());
    }

    template<class Archive> void serialize(Archive& archive, Slic3r::Domain::ConfigBox& box)
    {
        using namespace Slic3r::Domain;

        auto archive_item = [&archive](ConfigItem& item) {
            item.visit(overloaded(
                [&archive](EnumWrapper& ew) {
                    int val = ew.value();
                    archive(val);
                    if constexpr (Archive::is_loading::value)
                        ew.set_index(ew.index_of_value(val));
                },
                [&archive](EnumVectorWrapper& evw) {
                    auto vals = evw.get_indexes();
                    archive(vals);
                    if constexpr (Archive::is_loading::value)
                        evw.set_indexes(vals);
                },
                [&archive](auto& sth_else) {
                    archive(sth_else);
                }
            ));
        };

        for (ConfigItem& item : box.items.all_items())
            archive_item(item);
        for (ConfigItem& item : box.overrides.all_items())
            archive_item(item);
        {
            // Now load/save the active overrides.
            std::vector<std::string> overridden_names;
            if (Archive::is_saving::value) {
                const auto& overridden_items = box.overrides.overridden_items();
                for (const ConfigItem& item : overridden_items)
                    overridden_names.emplace_back(item.name());
            }
            archive(overridden_names);
            if (Archive::is_loading::value) {
                for (const std::string& name : overridden_names)
                    box.overrides.enable(name);
            }
        }
    }

    template <class Archive>
    struct specialize<Archive, Slic3r::Domain::Expr::ExprAst, cereal::specialization::non_member_serialize> {};

    // Custom non-member serialize for ExprAst
    template <class Archive>
    void serialize(Archive& ar, Slic3r::Domain::Expr::ExprAst& variant)
    {
        if constexpr (Archive::is_saving::value) {
            ar(variant.which());
            boost::apply_visitor([&](const auto& value) {
                ar(value);
            }, variant);
        } else {
            int which;
            ar(which);
            switch (which)
            {
                case 0: { bool t; ar(t); variant = t; break; }
                case 1: { double t; ar(t); variant = t; break; }
                case 2: { std::string t; ar(t); variant = t; break; }
                case 3: { Slic3r::Domain::Expr::RegEx t; ar(t); variant = t; break; }
                case 4: { Slic3r::Domain::Expr::Binary t; ar(t); variant = t; break; }
                case 5: { Slic3r::Domain::Expr::Unary t; ar(t); variant = t; break; }
                case 6: { Slic3r::Domain::Expr::FuncCall t; ar(t); variant = t; break; }
                case 7: { Slic3r::Domain::Expr::VarRef t; ar(t); variant = t; break; }
                default: PANIC("Invalid type index for ExprAst variant");
            }
        }
    }

    template <class Archive>
    struct specialize< Archive, Slic3r::Domain::Preset::RootPresetNode, cereal::specialization::non_member_serialize> {};

    template<class Archive> void serialize(Archive& archive, Slic3r::Domain::Preset::RootPresetNode& node)
    {
        archive(cereal::base_class<Slic3r::Domain::Preset::PresetNode>(&node), node.kind, node.origin, node.user_file);
    }

    template <class Archive> void serialize(Archive & ar, const std::nullptr_t &) {
        // We don't need to read any data; the object is already a nullptr.
        // We don't need to write any data to represent a null value.
    }



    template <class Archive>
    struct specialize<Archive, Slic3r::Domain::TriangleMesh, cereal::specialization::non_member_load_save>
    {};

    template <class Archive>
    void load(Archive& archive, Slic3r::Domain::TriangleMesh& mesh)
    {
        archive.loadBinary(
            reinterpret_cast<char*>(const_cast<Slic3r::Domain::TriangleMeshStats*>(&mesh.stats())),
            sizeof(Slic3r::Domain::TriangleMeshStats)
        );
        archive(mesh.its.indices, mesh.its.vertices);
    }

    template <class Archive>
    void save(Archive& archive, const Slic3r::Domain::TriangleMesh& mesh)
    {
        archive.saveBinary(
            reinterpret_cast<const char*>(&mesh.stats()),
            sizeof(Slic3r::Domain::TriangleMeshStats)
        );
        archive(mesh.its.indices, mesh.its.vertices);
    }

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Expr::Binary& value)
{
    archive(value.op, value.left, value.right);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Expr::Unary& value)
{
    archive(value.op, value.expr);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Expr::FuncCall& value)
{
    archive(value.name, value.args);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Expr::VarRef& value)
{
    archive(value.name);
}

template <class Archive>
void save(Archive& archive, const Slic3r::Domain::Expr::RegEx& value)
{
    archive(value.m_source);
}

template <class Archive>
void load(Archive& archive, Slic3r::Domain::Expr::RegEx& value)
{
    archive(value.m_source);
    value.m_regex = value.m_source;
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::ObjectID& value)
{
    archive(value.id);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::PresetNode& node)
{
    archive(
        node.id,
        node.name,
        node.inherits,
        node.unconditional_inherits,
        node.condition,
        node.match_mode,
        node.values,
        node.features,
        node.variants,
        node.source_location
    );
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::PresetName& name)
{
    std::vector<std::string> ids{name.id.begin(), name.id.end()};
    std::vector<std::string> root_ids{name.root_id.begin(), name.root_id.end()};
    archive(name.name, ids, name.origin);
    name.id = std::set<std::string>{ids.begin(), ids.end()};
    name.root_id = std::set<std::string>{root_ids.begin(), root_ids.end()};
}

template <class Archive, class ConfigFdmType, class ConfigSlaType>
void serialize(
    Archive& archive,
    Slic3r::Domain::Preset::EvaluatedPreset<ConfigFdmType, ConfigSlaType>& preset
)
{
    archive(
        preset.kind,
        preset.root_id,
        preset.id,
        preset.name,
        preset.origin,
        preset.values,
        preset.features,
        preset.conditions,
        preset.user_file,
        preset.last_node_location
    );
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::EvaluatedToolPrintPreset& preset)
{
    archive(preset.preset);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::EvaluatedMaterialPreset& preset)
{
    archive(preset.preset);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::EvaluatedPrintPreset& preset)
{
    archive(preset.preset, preset.tools, preset.materials);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::EvaluatedPrinterPreset& preset)
{
    archive(preset.hw_config, preset.preset, preset.prints);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::VendorBundle& bundle)
{
    archive(bundle.vendor_data, bundle.presets, bundle.preset_names, bundle.printer_configs);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::Bundle& bundle)
{
    archive(bundle.vendor_bundles, bundle.printer_configs, bundle.evaluated_presets);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::HwModel& model)
{
    archive(model.model, model.base_model);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::HwToolConfig& config)
{
    archive(config.id, config.name, config.features);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::HwFeederConfig& config)
{
    archive(config.id, config.type, config.model, config.slot_count, config.features);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::MaterialConfig& config)
{
    archive(config.id, config.type, config.features);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::HwSheetConfig& config)
{
    archive(config.id, config.name, config.type, config.features);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::VisualRepresentation& visual)
{
    archive(visual.bed_model, visual.bed_texture, visual.thumbnail);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::HwPrinterConfig& config)
{
    archive(
        config.id,
        config.printer_id,
        config.legacy_printer_model,
        config.vendor_id,
        config.repo_id,
        config.repo_version,
        config.name,
        config.short_name,
        config.technology,
        config.model,
        config.tool_count,
        config.features,
        config.visual,
        config.tools,
        config.feeders,
        config.materials,
        config.sheet
    );
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::FeatureDef& feature)
{
    archive(feature.default_value, feature.allowed_values, feature.user_editable);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::HwPrinterConfigDef& def)
{
    archive(
        def.id,
        def.name,
        def.technology,
        def.model,
        def.features,
        def.legacy_printer_model,
        def.tool_count,
        def.visual
    );
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::HwToolConfigDef& def)
{
    archive(def.id, def.name, def.technology, def.condition, def.features);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::HwFeederConfigDef& def)
{
    archive(
        def.id,
        def.name,
        def.technology,
        def.type,
        def.condition,
        def.model,
        def.features,
        def.slot_count
    );
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::HwSheetConfigDef& def)
{
    archive(def.id, def.name, def.type, def.condition, def.features);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::HwPrinterTechnologyDefs& defs)
{
    archive(defs.technology, defs.printers, defs.tools, defs.feeders, defs.sheets);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::HwToolConfigTemplate& templ)
{
    archive(templ.id, templ.features);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::HwFeederConfigTemplate& templ)
{
    archive(templ.id, templ.address, templ.features);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::HwPrinterConfigTemplate& templ)
{
    archive(
        templ.id,
        templ.name,
        templ.printer,
        templ.legacy_printer_model,
        templ.sheet,
        templ.tool_count,
        templ.features,
        templ.tools,
        templ.feeders,
        templ.visual
    );
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::VendorFeatures& features)
{
    archive(features.printer, features.tool, features.feeder, features.sheet);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::VendorInfo& info)
{
    archive(info.id, info.repo_id, info.name, info.version, info.features);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::VendorData& data)
{
    archive(data.info, data.printer_configs, data.defs);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::SourceLocation& location)
{
    archive(location.file, location.line, location.column);
}

template <class Archive, class T>
void serialize(Archive& archive, Slic3r::Domain::Preset::SourceLocated<T>& located)
{
    archive(located.value, located.source_location);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Preset::ParsedExpr& parsed_expr)
{
    archive(parsed_expr.expr, parsed_expr.expr_str);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::EmbossProjection& value)
{
    archive(value.depth, value.use_surface);
}

template <class Archive>
void save(Archive& archive, const Slic3r::Domain::EmbossShape::SvgFile& value)
{
    archive(
        value.path,
        value.path_in_3mf,
        (value.file_data != nullptr) ? *value.file_data : std::string("")
    );
}

template <class Archive>
void load(Archive& archive, Slic3r::Domain::EmbossShape::SvgFile& value)
{
    std::string data;
    archive(value.path, value.path_in_3mf, data);
    if (!data.empty())
        value.file_data = std::make_unique<std::string>(data);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::Percentage& percentage)
{
    archive(percentage.value);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::FloatOrPercentage& value)
{
    archive(value.m_value, value.m_is_percentage);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::ModelWipeTower& tower)
{
    archive(tower.position, tower.rotation);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::LayerHeightRange& range)
{
    archive(range.first, range.second);
}

template <class Archive>
void serialize(Archive& archive, Slic3r::Domain::FontProp::Align& align)
{
    archive(align.horizontal, align.vertical);
}
} // namespace cereal
