#include "Slic3r/App/Lua/ProjectApi.hpp"

#include "Slic3r/Directories.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Math.hpp"
#include "Slic3r/App/Config/ConfigItemControl.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Emboss/EmbossJob.hpp"
#include "Slic3r/Biz/Emboss/SvgShapeProvider.hpp"
#include "Slic3r/Biz/Emboss/TextPresetManager.hpp"
#include "Slic3r/Biz/Emboss/TextShapeProvider.hpp"
#include "Slic3r/Biz/Format/STL.hpp"
#include "Slic3r/Biz/Lua/LuaException.hpp"

#include <fmt/ranges.h>

//--@file _globals.lua
//--@meta _G

namespace Slic3r::App::Lua {

struct BoundingBox
{
    Domain::BoundingBox3d bounds;

    double min_x() const { return bounds.min.x(); }
    double min_y() const { return bounds.min.y(); }
    double min_z() const { return bounds.min.z(); }

    double max_x() const { return bounds.max.x(); }
    double max_y() const { return bounds.max.y(); }
    double max_z() const { return bounds.max.z(); }
};

struct Mesh
{
    Domain::TriangleMesh mesh;

    void translate(float x, float y, float z)
    {
        its_translate(mesh.its, Domain::Vec3f(x, y, z));
    }

    BoundingBox bounds() const
    {
        return {Domain::bounding_box(mesh)};
    }
};

enum class ElementType
{
    Object, Volume, Instance
};

struct ModelElement
{
    size_t project_id;
    Domain::ElementRef ref;

    ElementType type() const
    {
        if (ref.has_volume()) {
            return ElementType::Volume;
        }
        if (ref.has_instance()) {
            return ElementType::Instance;
        }
        return ElementType::Object;
    }
};

struct BedInstRef
{
    Biz::ProjectInteractor& project_interactor;
    size_t project_id;
    Domain::BedRef ref;

    const auto& config_container() const
    {
        const auto& proj = project_interactor.project(project_id);
        const auto* cc_ptr = proj.find_config_container(ref.config_container_id);
        if (cc_ptr == nullptr) {
            throw Biz::Lua::LuaException("Invalid bed instance reference");
        }
        return *cc_ptr;
    }

    auto& config_container()
    {
        auto& proj = project_interactor.project(project_id);
        auto* cc_ptr = proj.find_config_container(ref.config_container_id);
        if (cc_ptr == nullptr) {
            throw Biz::Lua::LuaException("Invalid bed instance reference");
        }
        return *cc_ptr;
    }

    const Domain::Preset::HwPrinterConfig& printer_config() const
    {
        return config_container().selected_preset().hw_config;
    }

    Domain::ConfigBox& printer_presets()
    {
        return config_container().mutable_selected_preset().printer.config_box();
    }

    Domain::ConfigBox& print_presets()
    {
        return config_container().mutable_selected_preset().print.config_box();
    }

    Domain::ConfigBox& tool_print_presets(size_t tool_idx)
    {
        return config_container().mutable_selected_preset().tools.at(tool_idx).config_box();
    }

    Domain::ConfigBox& material_presets(size_t slot_idx)
    {
        return config_container().mutable_selected_preset().materials.at(slot_idx).config_box();
    }
};

namespace {

std::tuple<double, bool> parse_pct(const std::string& s)
{
    bool is_pct = s.ends_with('%');
    double val = std::stod(is_pct ? s.substr(0, s.length() - 1) : s);
    return {val, is_pct};
}

Domain::Percentage parse_percentage(const sol::object& percentage)
{
    if (percentage.is<std::string>()) {
        auto pct = percentage.as<std::string>();
        const auto [val, is_pct] = parse_pct(percentage.as<std::string>());
        return {val};
    }
    if (percentage.is<double>()) {
        return Domain::Percentage{percentage.as<double>()};
    }
    if (percentage.is<float>()) {
        return Domain::Percentage{percentage.as<float>()};
    }
    return {percentage.as<double>()};
}

Domain::FloatOrPercentage parse_float_or_percentage(const sol::object& o)
{
    if (o.is<std::string>()) {
        const auto [val, is_pct] = parse_pct(o.as<std::string>());
        if (is_pct)
            return Domain::Percentage{val};
        return {val};
    }
    return {o.as<double>()};
}

bool set_param(Domain::ConfigBox& settings, const std::string& name, const sol::object& val)
{
    auto it      = settings.find(name);
    bool success = false;
    if (it.item) {
        success = true;
        it.item->visit(
            Domain::overloaded{
                [&val](double& dest_val)
                {
                    dest_val = val.as<double>();
                },
                [&val](int& dest_val)
                {
                    dest_val = val.as<int>();
                },
                [&val](Domain::Percentage& dest_val)
                {
                    dest_val = parse_percentage(val);
                },
                [&val](Domain::FloatOrPercentage& dest_val)
                {
                    dest_val = parse_float_or_percentage(val);
                },
                [&val, &it](Domain::EnumWrapper& dest_val)
                {
                    std::string enum_name = val.as<std::string>();
                    const bool contains   = std::ranges::any_of(
                        dest_val.def(),
                        [&enum_name](const Domain::EnumValueDef& d)
                        { return d.str_serialized == enum_name; }
                    );
                    if (contains) {
                        dest_val.set_string(enum_name);
                    } else {
                        SPDLOG_ERROR(
                            "Unknown enum value {} for {}, allowed values are: {}",
                            enum_name,
                            it.item->def().name,
                            fmt::join(
                                dest_val.def()
                                    | std::views::transform(
                                        [](const Domain::EnumValueDef& def) -> std::string_view
                                        { return def.str_serialized; }
                                    ),
                                ", "
                            )
                        );
                    }
                },
                [&success](auto& val)
                {
                    success = false;
                }
            }
        );
        if (it.is_override) {
            settings.overrides.enable(name);
        }
    }

    return success;
}

} // namespace

void update_volume_from_def(Domain::ModelVolume* added_vol, const sol::table& def)
{
    if (def["translate"].valid()) {
        sol::table translate = def["translate"];
        double tx            = translate.get_or("x", 0.0);
        double ty            = translate.get_or("y", 0.0);
        double tz            = translate.get_or("z", 0.0);
        added_vol->set_offset(Domain::Vec3d{tx, ty, tz});
    }
    if (def["rotate"].valid()) {
        sol::table rot = def["rotate"];
        double rx      = deg2rad(rot.get_or("x", 0.0));
        double ry      = deg2rad(rot.get_or("y", 0.0));
        double rz      = deg2rad(rot.get_or("z", 0.0));
        added_vol->set_rotation(Domain::Vec3d{rx, ry,  rz});
    }
    if (def["params"].valid()) {
        sol::table params = def["params"];
        params.for_each(
            [&](const sol::object& key, const sol::object& val)
            {
                auto& settings = added_vol->volume_settings;
                auto name      = key.as<std::string>();
                if (!set_param(settings, name, val)) {
                    SPDLOG_ERROR("Cannot set volume settings '{}': not found", name);
                }
            }
        );
    }
}

void update_object_from_def(Domain::ModelObject& mo, const sol::table& def)
{
    if (def["object_params"].valid()) {
        sol::table params = def["object_params"];
        params.for_each(
            [&](const sol::object& key, const sol::object& val)
            {
                auto& settings = mo.object_settings;
                auto name      = key.as<std::string>();
                if (!set_param(settings, name, val)) {
                    SPDLOG_ERROR("Cannot set object settings '{}': not found", name);
                }
            }
        );
    }
}

void add_volume_to_object(const sol::table& def, Domain::ModelObject& obj)
{
    const Mesh& mesh                 = def["mesh"];
    auto mesh_copy                   = mesh.mesh;
    const bool has_params            = def["params"].valid();
    Domain::ModelVolumeType vol_type = def.get_or(
        "type",
        has_params ? Domain::ModelVolumeType::PARAMETER_MODIFIER :
                     Domain::ModelVolumeType::MODEL_PART
    );
    auto* added_vol = Biz::Algorithms::ModelObject::add_volume(&obj, std::move(mesh_copy), vol_type);
    update_volume_from_def(added_vol, def);
}

Mesh load_stl(Biz::Lua::LuaEngine& lua, const std::string& path)
{
    const auto resolved_path = lua.resolve_file(path);
    if (resolved_path.empty()) {
        throw Biz::Lua::LuaException("Cannot safely load file: {}", path);
    }
    auto ret = Biz::load_stl(resolved_path);
    if (!ret.has_value()) {
        throw Biz::Lua::LuaException{ret.error()};
    }
    return {*ret};

}


struct ProjectLuaApi
{
    Biz::ProjectInteractor& project_interactor;
    size_t project_id;

    ModelElement add_object(const sol::table& def)
    {
        const Mesh& mesh = def["mesh"];

        auto& scene_interactor = project_interactor.scene_interactor();

        Biz::Scene::SceneInteractor::UpdateObjectFn update_fn = nullptr;
        std::optional<Domain::Vec3d> center_at;
        const auto& bed_sel = scene_interactor.bed_selection();
        if (!bed_sel.empty()) {
            const auto* bed_inst =
                project_interactor.project(project_id)
                    .find_bed_instance_by_id(bed_sel.last_selected_bed().instance_id);
            ASSERT(bed_inst);

            const auto& center =  bed_inst->bed.get().center();
            const auto center4 = Domain::Vec4d(center.x(), center.y(), 0., 1.);
            const auto xformed_center = bed_inst->transformation.get_matrix() * center4;
            center_at = Domain::Vec3d(xformed_center.x() / xformed_center.w(), xformed_center.y() / xformed_center.w(), 0);
        }
        update_fn = [&center_at, &def](Domain::ModelObject& obj)
        {
            update_object_from_def(obj, def);
            update_volume_from_def(obj.volumes.front(), def);

            const auto& other_volumes = def.get<std::optional<sol::table>>("other_volumes");
            if (other_volumes.has_value()) {
                other_volumes->for_each([&obj](const sol::object&, const sol::table& def)
                                       { add_volume_to_object(def, obj); });

                Biz::Algorithms::ModelObject::sort_volumes(&obj);
                obj.invalidate_bounding_box();

            }
            if (center_at.has_value()) {
                const auto& bb = Biz::Algorithms::ModelObject::bounding_box_exact(obj);
                auto center = Domain::Vec3d{(bb.max - bb.min) / 2.0 + bb.min};
                center.z() = 0.0;

                auto& inst = obj.instances.front();
                Domain::Transformation xform{inst->get_matrix()};
                xform.set_offset(*center_at - center);
                inst->set_transformation(xform);
            }
        };

        auto mesh_copy = mesh.mesh;
        scene_interactor.new_object_from_mesh(std::move(mesh_copy), project_id, update_fn);
        const auto& sel = scene_interactor.object_selection(project_id);

        return {project_id, sel.elements.front()};
    }

    ModelElement add_volume(const ModelElement& target, Domain::ModelVolumeType vol_type, const Mesh& mesh)
    {
        auto mesh_copy = mesh.mesh;
        auto& scene_interactor = project_interactor.scene_interactor();
        Domain::ElementRef added_vol_ref = target.ref;
        scene_interactor.add_volume(project_id, target.ref.instance_id, [&](Domain::ModelObject& obj)-> Domain::ModelVolume*
        {
            std::unique_ptr<Domain::ModelVolume> vol{Biz::Algorithms::ModelVolume::construct_ptr(nullptr, mesh_copy, vol_type)};
            auto* added_vol = Biz::Algorithms::ModelObject::add_volume(&obj, mesh.mesh);
            added_vol->set_type(vol_type);
            added_vol_ref.volume_id = added_vol->id().id;
            return added_vol;
        });
        return {project_id, added_vol_ref};
    }

    void clear_layer_custom_steps(const BedInstRef& bed_inst_ref)
    {
        Domain::BedInstance* inst = project_interactor.project(bed_inst_ref.project_id).find_bed_instance_by_id(bed_inst_ref.ref.instance_id);
        if (inst == nullptr) {
            throw Biz::Lua::LuaException("Invalid bed instance reference");
        }

        if (inst->custom_gcode.has_value()) {
            inst->custom_gcode->gcodes.clear();
        }
    }

    void insert_layer_custom_gcode(const BedInstRef& bed_inst_ref, double z_depth, const std::string& gcode)
    {
        Domain::BedInstance* inst = project_interactor.project(bed_inst_ref.project_id).find_bed_instance_by_id(bed_inst_ref.ref.instance_id);
        if (inst == nullptr) {
            throw Biz::Lua::LuaException("Invalid bed instance reference");
        }

        if (inst->custom_gcode == std::nullopt) {
            inst->custom_gcode = Domain::CustomGCode::Info{};
        }
        Domain::CustomGCode::Item item{
            .print_z  = z_depth,
            .type     = Domain::CustomGCode::Type::Custom,
            .extruder = 1,
            .extra    = gcode
        };
        inst->custom_gcode->gcodes.emplace_back(std::move(item));
    }

    BedInstRef current_bed() const
    {
        auto bed_inst_id =
            project_interactor.scene_interactor().bed_selection(project_id)->last_selected_bed();
        return {project_interactor, project_id, bed_inst_id};
    }
};


Mesh create_cube(double width, double height, double depth)
{
    return {Biz::Algorithms::TriangleMesh::its_make_cube(width, height, depth)};
}

Mesh create_sphere(double radius, std::optional<double> fa)
{
    return {Biz::Algorithms::TriangleMesh::its_make_sphere(radius, deg2rad(fa.value_or(1)))};
}

Mesh create_cylinder(double radius, double height, std::optional<double> fa)
{
    return {
        Biz::Algorithms::TriangleMesh::its_make_cylinder(radius, height, deg2rad(fa.value_or(1)))
    };
}

Mesh create_cone(double radius, double height, std::optional<double> fa)
{
    return {Biz::Algorithms::TriangleMesh::its_make_cone(radius, height, deg2rad(fa.value_or(1)))};
}

Mesh create_tetrahedron(double size)
{
    return {Biz::Algorithms::TriangleMesh::its_make_tetrahedron(size)};
}

Mesh create_prism(double width, double length, double height)
{
    return {Biz::Algorithms::TriangleMesh::its_make_prism(width, length, height)};
}

Mesh create_frustum(double radius, double height, std::optional<double> fa)
{
    return {Biz::Algorithms::TriangleMesh::its_make_frustum(radius, height, deg2rad(fa.value_or(1)))};
}

Mesh create_frustum_dowel(double radius, double height, int sector_count)
{
    return {Biz::Algorithms::TriangleMesh::its_make_frustum_dowel(radius, height, sector_count)};
}

Mesh create_pyramid(double base, double height)
{
    return {Biz::Algorithms::TriangleMesh::its_make_pyramid(base, height)};
}

Mesh create_snap(
    double radius,
    double height,
    std::optional<double> space_proportion,
    std::optional<double> bulge_proportion
)
{
    return {Biz::Algorithms::TriangleMesh::its_make_snap(
        radius,
        height,
        space_proportion.value_or(0.25),
        bulge_proportion.value_or(0.125)
    )};
}

Mesh create_torus(
    double r,
    double t,
    std::optional<double> ra,
    std::optional<double> ta
)
{
    return {Biz::Algorithms::TriangleMesh::its_make_torus(
        r,
        t,
        deg2rad(ra.value_or(1)),
        deg2rad(ta.value_or(1))
    )};
}



Mesh emboss_svg(Biz::Lua::LuaEngine& lua, const std::string& file_path, double depth)
{
    Domain::EmbossShape shape;
    shape.projection.depth = depth;
    shape.svg_file = {.path=lua.resolve_file(file_path)};

    if (!shape.svg_file->path.empty()) {
        auto read_result = Biz::Emboss::read_shape_from_file(shape, {}, {});
        if (read_result != Biz::Emboss::ReadShapeResult::success) {
            return {};
        }
        auto svg_provider = std::make_unique<Biz::Emboss::SvgShapeProvider>(shape, Biz::Emboss::Scale{{},{},{}});
        Biz::Emboss::TriMeshBaseData tri_mesh_data{
            .shape_provider = std::move(svg_provider),
            .is_outside = true
        };
        auto result = Biz::Emboss::create_mesh(tri_mesh_data);
        if (result.has_value()) {
            return {result.value()};
        }
    }
    return {};
}

struct EmbossTextOpts
{
    const Domain::FontDescriptor& font;
    const std::string text;
    double depth{1.0};
    Domain::FontProp font_prop;
};

template <typename T>
void set_font_prop(const sol::table& def, std::string_view key, std::function<T&()>&& accessor)
{
    if (def[key].valid()) {
        accessor() = def[key].get<T>();
    }
}

EmbossTextOpts parse_emboss_text_opts(const sol::table& def)
{
    Domain::FontProp font_prop;

    set_font_prop<float>(def, "line_height", [&]() -> auto& { return font_prop.size_in_mm; });
    set_font_prop<bool>(def, "per_glyph", [&]() -> auto& { return font_prop.per_glyph; });

    return {
        .font = def.get<Domain::FontDescriptor>("font"),
        .text = def.get<std::string>("text"),
        .font_prop = std::move(font_prop)
    };
}

Mesh emboss_text(
    Biz::Emboss::TextPresetManager& preset_manager,
    const EmbossTextOpts& opts
)
{
    Domain::EmbossShape shape;
    shape.projection.depth = opts.depth;
#if 1
    preset_manager.set_font(opts.font);
    Biz::Emboss::TextPresetManager::Preset preset = preset_manager.get_preset();
    preset.emboss_style.prop = opts.font_prop;
    Domain::TextConfiguration text_cfg{ .style = preset.emboss_style, .text = opts.text };
    Biz::Emboss::TextLines text_lines;
    Biz::Emboss::FontFileWithCache& font_file = preset_manager.get_font_file_with_cache();
    auto text_provider = std::make_unique<Biz::Emboss::TextShapeProvider>(text_cfg, shape.projection, text_lines, font_file);
    Biz::Emboss::TriMeshBaseData tri_mesh_data{
        .shape_provider = std::move(text_provider),
        .is_outside = true
    };
    auto result = Biz::Emboss::create_mesh(tri_mesh_data);
    if (result.has_value()) {
        return {result.value()};
    }
#endif
    return {};
}

sol::object feature_value(sol::state_view lua, const Domain::Preset::FeatureValue& val) {
    // Visit the underlying variant (JsonValue inherits from JsonVariant)
    return std::visit([&lua](const auto& arg) -> sol::object {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            return sol::lua_nil;
        }
        else if constexpr (std::is_same_v<T, bool> ||
                           std::is_same_v<T, double> ||
                           std::is_same_v<T, std::string>) {
            return sol::make_object(lua, arg);
        }
        else if constexpr (std::is_same_v<T, Domain::JsonArray>) {
            // Convert C++ vector to a Lua array-like table
            sol::table t = lua.create_table();
            for (size_t i = 0; i < arg.size(); ++i) {
                // Note: Lua arrays are 1-indexed, so we use i + 1
                t[i + 1] = feature_value(lua, arg[i]);
            }
            return t;
        }
        else if constexpr (std::is_same_v<T, Domain::JsonObject>) {
            // Convert C++ map to a Lua dictionary-like table
            sol::table t = lua.create_table();
            for (const auto& [key, value] : arg) {
                t[key] = feature_value(lua, value);
            }
            return t;
        }
        else {
            // Fallback (should be unreachable given your variant types)
            return sol::lua_nil;
        }
    }, static_cast<const Domain::JsonVariant&>(val));
}

sol::object feature_value(
    sol::this_state lua,
    const Domain::Preset::FeatureValueMap& features,
    const std::string& name
)
{
    auto it = features.find(name);
    if (it == features.end()) {
        return sol::lua_nil;
    }
    return feature_value(lua, it->second);
}

using ExposedConfigValue = std::variant<
    bool,
    int,
    std::optional<int>,
    double,
    std::string,
    Domain::Vec2d,
    Domain::FloatOrPercentage,
    Domain::Percentage>;

ProjectApi::ProjectApi(
    Biz::ProjectInteractor& project_interactor,
    Biz::Emboss::IFontManager& font_manager
) :
    m_project_interactor(project_interactor),
    m_font_manager(font_manager),
    m_text_preset_manager(
        font_manager,
        Slic3r::data_dir() + "/text_emboss_presets_scripts.cereal",
        project_interactor
    ),
    m_fav_fonts(font_manager.create_favorit())
{
    m_text_preset_manager.init();
}

void ProjectApi::register_api(Biz::Lua::LuaEngine& lua)
{
    auto& state = lua.state();
    //--@class BoundingBox
    //--@field min_x number
    //--@field max_x number
    //--@field min_y number
    //--@field max_y number
    //--@field min_z number
    //--@field max_z number
    //- local BoundingBox = {}
    state.new_usertype<BoundingBox>(
        "BoundingBox", sol::no_constructor,
        "min_x", sol::property(&BoundingBox::min_x),
        "min_y", sol::property(&BoundingBox::min_y),
        "min_z", sol::property(&BoundingBox::min_z),
        "max_x", sol::property(&BoundingBox::max_x),
        "max_y", sol::property(&BoundingBox::max_y),
        "max_z", sol::property(&BoundingBox::max_z)
    );

    //--@class Mesh
    //- local Mesh = {}

    //-- Translates the mesh in 3D space by the given offsets.
    //--@param x number The X offset.
    //--@param y number The Y offset.
    //--@param z number The Z offset.
    //- function Mesh:translate(x, y, z) end

    //-- Calculates and returns the bounding box of the mesh.
    //--@return BoundingBox bounds The computed bounding box.
    //- function Mesh:bounds() end
    state.new_usertype<Mesh>("Mesh", sol::no_constructor, "translate", &Mesh::translate, "bounds", &Mesh::bounds);

    //--@class ModelElement
    //--@field type integer The element type identifier.
    //- local ModelElement = {}
    state.new_usertype<ModelElement>("ModelElement", sol::no_constructor, "type", sol::property(&ModelElement::type));

    //--@class ConfigBox
    //- local ConfigBox = {}

    //-- Retrieves a preset/configuration value by name.
    //--@param name string The config key.
    //--@return boolean|integer|number|string|table value
    //- function ConfigBox:value(name) end

    //-- Sets a preset/configuration value by name.
    //--@param name string The config key.
    //--@param value any The value to set.
    //- function ConfigBox:set(name, value) end
    state.new_usertype<Domain::ConfigBox>("ConfigBox", sol::no_constructor,
        "value", [](const Domain::ConfigBox& config, const std::string& name) -> ExposedConfigValue
        {
            const auto it = config.find(name);
            if (!it.item) {
                throw Biz::Lua::LuaException(
                    fmt::format("Invalid preset item name '{}': not found", name)
                );
            }
            return it.item->visit(
                Domain::overloaded{
                    []<typename T>(const T& item) -> ExposedConfigValue
                    requires Domain::is_in_variant<T, ExposedConfigValue>::value
                    { return item; },

                    []<typename T>(const T&) -> ExposedConfigValue
                    requires (!Domain::is_in_variant<T, ExposedConfigValue>::value)
                    {
                        throw Biz::Lua::LuaException("Unsupported config type");
                    }
                }
            );
        },
        "set", [](Domain::ConfigBox& config, const std::string& name, const sol::object& value)
        {
            set_param(config, name, value);
        }
    );


    //--@class HwToolConfig
    //- local HwToolConfig = {}
    state.new_usertype<Domain::Preset::HwToolConfig>(
        "HwToolConfig",
        sol::no_constructor,
        //-- Gets feature value
        //--@param name string feature name
        //--@return boolean|integer|number|string|table value
        //- function HwToolConfig:feature(name) end
        "feature",
        [](sol::this_state state,
           const Domain::Preset::HwToolConfig& config,
           const std::string& name) -> sol::object
        { return feature_value(state, config.features, name); },
        "nozzle_diameter",
        [](sol::this_state state, const Domain::Preset::HwToolConfig& config) -> std::optional<double>
        {
            auto val = feature_value(state, config.features, "nozzle_diameter");
            //return val == sol::lua_nil ? 0.4 : val.as<double>();
            return val == sol::lua_nil ? std::nullopt : std::make_optional(val.as<double>());
        }

    );

    state.new_usertype<Domain::Preset::HwPrinterConfig>(
        "HwPrinterConfig",
        sol::no_constructor,
        "name", sol::readonly_property(&Domain::Preset::HwPrinterConfig::name),
        "tool_count", sol::readonly_property(&Domain::Preset::HwPrinterConfig::tool_count),
        "tools", sol::readonly_property(&Domain::Preset::HwPrinterConfig::tools)
    );

    //--@class BedInstRef
    //- local BedInstRef = {}
    //--@return ConfigBox
    //- function BedInstRef:printer_presets() end
    //--@return ConfigBox
    //- function BedInstRef:print_presets() end
    //--@param tool_idx integer
    //--@return ConfigBox
    //- function BedInstRef:tool_print_presets(tool_idx) end
    //--@param slot_idx integer
    //--@return ConfigBox
    //- function BedInstRef:material_presets(slot_idx) end
    state.new_usertype<BedInstRef>("BedInstRef", sol::no_constructor,
        "printer_config", &BedInstRef::printer_config,
        "printer_presets", &BedInstRef::printer_presets,
        "print_presets", &BedInstRef::print_presets,
        "tool_print_presets", &BedInstRef::tool_print_presets,
        "material_presets", &BedInstRef::material_presets
    );

    //--@class FontDescriptor
    //--@field name string
    //- local FontDescriptor = {}
    state.new_usertype<Domain::FontDescriptor>("Font", sol::no_constructor,
        "name", sol::property(&Domain::FontDescriptor::name)
    );

    //-- Defines the behavior or role of a specific volume within a model.
    //--@enum VolumeType
    //- VolumeType = {
    //-- A standard solid model part that will be printed.
    //-     Solid = 1,
    //-- A volume used to subtract geometry from solid parts.
    //-     Negative = 2,
    //-- A volume that modifies print parameters where it intersects the model.
    //-     Modifier = 3,
    //-- Prevents supports from being generated within this volume.
    //-     SupportBlocker = 4,
    //-- Forces supports to be generated within this volume.
    //-     SupportEnforcer = 5,
    //-- An invalid or uninitialized volume state.
    //-     Invalid = 6
    //- }
    state.new_enum<Domain::ModelVolumeType>(
        "VolumeType",
        {{"Solid", Domain::ModelVolumeType::MODEL_PART},
         {"Negative", Domain::ModelVolumeType::NEGATIVE_VOLUME},
         {"Modifier", Domain::ModelVolumeType::PARAMETER_MODIFIER},
         {"SupportBlocker", Domain::ModelVolumeType::SUPPORT_BLOCKER},
         {"SupportEnforcer", Domain::ModelVolumeType::SUPPORT_ENFORCER},
         {"Invalid", Domain::ModelVolumeType::INVALID}}
    );



    // =========================================================================
    // API Shapes & Definitions
    // =========================================================================

    //--@class Vector3Shape
    //--@field x? number
    //--@field y? number
    //--@field z? number
    //- local Vector3Shape = {}

    //--@class VolumeDefinition
    //--@field mesh Mesh The geometry for this volume.
    //--@field translate? Vector3Shape Positional offset.
    //--@field rotate? Vector3Shape Rotation [degrees].
    //--@field type? VolumeType Defaults to ModelPart (or Modifier if params exist).
    //--@field params? table<string, any> Dictionary of volume-specific print settings.
    //- local VolumeDefinition = {}

    //--@class ObjectDefinition : VolumeDefinition
    //--@field object_params? table<string, any> Dictionary of object-specific print settings.
    //--@field other_volumes? VolumeDefinition[] Additional volumes attached to this object.
    //- local ObjectDefinition = {}

    //--@class ProjectApi
    //- local ProjectApi = {}

    //-- Adds a new object to the scene using the provided definition table.
    //--@param def ObjectDefinition The configuration table for the new object.
    //--@return ModelElement model The newly created object.
    //- function ProjectApi:add_object(def) end

    //-- Inserts custom G-Code at a specific Z-depth.
    //--@param bed_inst_ref BedInstRef The bed instance to target.
    //--@param z_depth number The Z-height to insert the G-Code.
    //--@param gcode string The G-Code string.
    //- function ProjectApi:insert_layer_custom_gcode(bed_inst_ref, z_depth, gcode) end

    //-- Clears all custom layer steps for the given bed instance.
    //--@param bed_inst_ref BedInstRef
    //- function ProjectApi:clear_layer_custom_steps(bed_inst_ref) end

    //-- Retrieves the currently active bed instance.
    //--@return BedInstRef
    //- function ProjectApi:current_bed() end
    state.new_usertype<ProjectLuaApi>("ProjectApi",
        sol::no_constructor,
        "add_object", &ProjectLuaApi::add_object,
        "insert_layer_custom_gcode", &ProjectLuaApi::insert_layer_custom_gcode,
        "clear_layer_custom_steps", &ProjectLuaApi::clear_layer_custom_steps,
        "current_bed", &ProjectLuaApi::current_bed
    );
    //-api = require("api")


    //--@file api.lua
    // =========================================================================
    // Global `api` Module Binding
    // =========================================================================
    //-- Slice Plugin API module
    //--@module api
    //- local api = {}
    auto api = state["api"].get_or_create<sol::table>();
    //--@type ProjectApi
    //- local projectApi = {}
    //- api.project = projectApi
    api["project"] = ProjectLuaApi(m_project_interactor, m_project_interactor.selected_project_id());
    //-- Creates a cube mesh.
    //--@param width number Width [mm]
    //--@param height number Height [mm]
    //--@param depth number Depth [mm]
    //--@return Mesh mesh A constructed geometry
    //- function api.make_cube(width, height, depth) end
    api["make_cube"] = &create_cube;

    //-- Creates a sphere mesh.
    //--@param radius number Radius [mm]
    //--@param fa? number Optional facet angle [degrees].
    //--@return Mesh mesh A constructed geometry
    //- function api.make_sphere(radius, fa) end
    api["make_sphere"] = &create_sphere;

    //-- Creates a cylinder mesh.
    //--@param radius number Radius [mm]
    //--@param height number Height [mm]
    //--@param fa? number Optional facet angle [degrees].
    //--@return Mesh mesh A constructed geometry
    //- function api.make_cylinder(radius, height, fa) end
    api["make_cylinder"] = &create_cylinder;

    //-- Creates a cone mesh.
    //--@param radius number Radius [mm]
    //--@param height number Height [mm]
    //--@param fa? number Optional facet angle [degrees].
    //--@return Mesh mesh A constructed geometry
    //- function api.make_cone(radius, height, fa) end
    api["make_cone"] = &create_cone;

    //-- Creates a tetrahedron mesh.
    //--@param size number Edge length [mm]
    //--@return Mesh mesh A constructed geometry
    //- function api.make_tetrahedron(size) end
    api["make_tetrahedron"] = &create_tetrahedron;

    //-- Creates a prism mesh.
    //--@param width number Width [mm]
    //--@param length number Lenght [mm]
    //--@param height number Height [mm]
    //--@return Mesh mesh A constructed geometry
    //- function api.make_prism(width, length, height) end
    api["make_prism"] = &create_prism;

    //-- Creates a frustum mesh.
    //--@param radius number Radius [mm]
    //--@param height number Height [mm]
    //--@param fa? number Optional facet angle [degrees].
    //--@return Mesh mesh A constructed geometry
    //- function api.make_frustum(radius, height, fa) end
    api["make_frustum"] = &create_frustum;

    //-- Creates a frustum dowel mesh.
    //--@param radius number Radius [mm]
    //--@param height number Height [mm]
    //--@param sector_count integer
    //--@return Mesh mesh A constructed geometry
    //- function api.make_frustum_dowel(radius, height, sector_count) end
    api["make_frustum_dowel"] = &create_frustum_dowel;

    //-- Creates a pyramid mesh.
    //--@param base number Base size [mm]
    //--@param height number Height [mm]
    //--@return Mesh mesh A constructed geometry
    //- function api.make_pyramid(base, height) end
    api["make_pyramid"] = &create_pyramid;

    //-- Creates a snap joint mesh.
    //--@param radius number Radius [mm]
    //--@param height number Height [mm]
    //--@param space_proportion? number
    //--@param bulge_proportion? number
    //--@return Mesh mesh A constructed geometry
    //- function api.make_snap(radius, height, space_proportion, bulge_proportion) end
    api["make_snap"] = &create_snap;

    //-- Creates a torus mesh.
    //--@param r number Major radius [mm].
    //--@param t number Minor radius [mm].
    //--@param ra? number Optional major facet angle [degrees].
    //--@param ta? number Optional minor facet angle [degrees].
    //--@return Mesh mesh A constructed geometry
    //- function api.make_torus(r, t, ra, ta) end
    api["make_torus"] = &create_torus;

    //-- Embosses an SVG file into a mesh.
    //--@param path string The path to the SVG file.
    //--@param depth number Extrusion depth [mm].
    //--@return Mesh mesh A constructed geometry
    //- function api.emboss_svg(path, depth) end
    api["emboss_svg"] = [&lua](const std::string& path, double depth)
    { return emboss_svg(lua, path, depth); };

    //-- Loads an STL file into a mesh.
    //--@param path string The path to the STL file.
    //--@return Mesh mesh A constructed geometry
    //- function api.load_stl(path) end
    api["load_stl"] = [&lua](const std::string& path){ return load_stl(lua, path); };

    //-- Retrieves a list of all available fonts.
    //--@return FontDescriptor[]
    //- function api.fonts() end
    api["fonts"] = [this]() -> const auto& { return m_font_manager.get_fonts(); };

    //-- Retrieves the default font descriptor.
    //--@return FontDescriptor
    //- function api.get_default_font() end
    api["get_default_font"] = [this]() -> Domain::FontDescriptor
    {
        return m_fav_fonts.front();
    };

    //-- Searches for a font by name.
    //--@param name string The name or partial name of the font.
    //--@return FontDescriptor
    //- function api.get_font(name) end
    api["get_font"] = [this](const std::string& name) -> Domain::FontDescriptor
    {
        auto fonts = m_font_manager.get_fonts();
        for (const auto& font : fonts) {
            auto it = font.name.find(name);
            if (it != std::string::npos) {
                return font;
            }
        }
        return m_fav_fonts.front();
    };

    //--@class EmbossTextOpts
    //--@field font FontDescriptor font to be used
    //--@field text string Text to emboss
    //--@field depth double Depth of emboss [mm]
    //--@field line_height double Height of single line text [mm]
    //- local EmbossTextOpts = {}

    //-- Embosses text into a mesh.
    //--@param opts EmbossTextOpts
    //--@return Mesh mesh A constructed geometry
    //- function api.emboss_text(opts) end
    api["emboss_text"] =
        [this](const sol::table& def)
        {
            auto opts = parse_emboss_text_opts(def);
            return emboss_text(m_text_preset_manager, opts);
        };
}

//- return api

} // namespace Slic3r::App::Lua
