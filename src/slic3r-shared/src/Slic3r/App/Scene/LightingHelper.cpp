#include "Slic3r/App/Scene/LightingHelper.hpp"
#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/App/Scene/ISceneProvider.hpp"
#include "Slic3r/App/Scene/Lights.hpp"
#include "Slic3r/App/Scene/BedRenderHelper.hpp"
#include "Slic3r/App/Scene/BedMaterials.hpp"
#include "Slic3r/App/Scene/NodeVisitor.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/App/Render/Material.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/App/Scene/BedNodeTag.hpp"
#include "Slic3r/Biz/Algorithms/Geometry/Geometry.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/App/Scene/GraphicsSettings.hpp"

#include <Slic3r/App/libvgcode/GCodeNodeTag.hpp>

#include <Slic3r/LegacyFormat.hpp>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace Slic3r::App::Scene {

void set_uniforms(const Lighting& lights, Render::Material& material)
{
    DEBUG_ASSERT(!lights.lights.empty());
    material.set_uniform("ambient_intensity", lights.ambient_intensity);
    material.set_uniform("num_lights", int(lights.lights.size()));
    int light_shadows_id = 0;
    for (size_t i = 0; i < lights.lights.size(); ++i) {
        const Light& light = lights.lights[i];
        if (light.shadows) {
            DEBUG_ASSERT(light_shadows_id == 0); // only one light should have shadows enabled
            light_shadows_id = int(i);
        }
        std::string light_str = format("lights[%zu]", i);
        material
            .set_uniform(light_str + ".system", int(light.system))
            .set_uniform(light_str + ".direction", light.direction)
            .set_uniform(light_str + ".diffuse", light.diffuse)
            .set_uniform(light_str + ".shadows", light.shadows)
            .set_uniform(light_str + ".ambient", light.ambient)
            .set_uniform(light_str + ".specular", light.specular)
            .set_uniform(light_str + ".shininess", light.shininess);
    }
    material.set_uniform("light_shadows_id", light_shadows_id);
}

void set_uniforms(const PBRParamsList& pbr_params, Render::Material& material)
{
    for (size_t i = 0; i < pbr_params.size(); ++i) {
        const PBRParams& params = pbr_params[i];
        std::string material_str = format("materials[%zu]", i);
        material
            .set_uniform(material_str + ".metal", params.metal)
            .set_uniform(material_str + ".roughness", params.roughness)
            .set_uniform(material_str + ".ior", params.ior);
    }
}

static std::pair<float, float> xyz_to_az(const Domain::Vec3f& xyz)
{
    if (xyz.isApprox(Domain::Vec3f::UnitZ()))
        return {0.0f, 0.0f};
    else if (xyz.isApprox(-Domain::Vec3f::UnitZ()))
        return {0.0f, float(M_PI)};

    std::pair<float, float> za = Biz::Algorithms::Geometry::dir_to_spheric(xyz);
    if (za.second < 0.0f)
        za.second += 2.0f * float(M_PI);
    return {za.second, za.first};
}

// #define GRAPHIC_SETTINGS_DEBUG

void render_imgui_graphics_settings_debug_window(const Domain::Project& project, const Render::Device& device, ISceneProvider& scene_provider,
    Render::ImguiRender& imgui_render)
{
#ifndef GRAPHIC_SETTINGS_DEBUG
    return;
#endif // GRAPHIC_SETTINGS_DEBUG

    float items_width = 150.0f;

    const char* items[]          = {"Shading", "Lights", "Bed"};
    static int item_selected_idx = 0;

    static bool reset_size = false;
    ImGuiWindow* wnd       = ImGui::FindWindowByName("Graphics settings");
    if (reset_size && wnd != nullptr) {
        // to avoid imgui 'animation' when the window content changes size
        wnd->DC.CursorMaxPos = wnd->DC.CursorStartPos;
        reset_size           = false;
        Biz::Platform::PlatformServices::instance().render_request_handler().request_render();
    }

    ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);
    ImGui::SetNextWindowPos({0.5f * ImGui::GetMainViewport()->Size.x, 50.0f}, ImGuiCond_Once, {0.5f, 0.0f});
    if (ImGui::Begin("Graphics settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar)) {
        if (ImGui::BeginTable("FPS", 2, ImGuiTableFlags_Borders)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("FPS");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.1f", ImGui::GetIO().Framerate);

            ImGui::EndTable();
        }

        if (ImGui::BeginTable("GraphicsCard", 2, ImGuiTableFlags_Borders)) {
            const Render::Context& ctx = Render::Context::instance();

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Vendor");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", ctx.gl_vendor_string().c_str());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Renderer");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", ctx.gl_renderer_string().c_str());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("OpenGL version");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", ctx.gl_version_string().c_str());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("OpenGL core profile");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", ctx.gl_core_profile_string().c_str());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("GLSL version");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", ctx.glsl_version_string().c_str());

            ImGui::EndTable();
        }

        ImGui::BeginGroup();
        if (ImGui::BeginListBox("##listbox", {100.0f, -1.0f})) {
            for (int i = 0; i < IM_ARRAYSIZE(items); ++i) {
                bool is_selected = (item_selected_idx == i);
                if (ImGui::Selectable(items[i], is_selected)) {
                    item_selected_idx = i;
                    reset_size        = true;
                }

                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndListBox();
        }
        ImGui::EndGroup();

        if (!reset_size) {
            ImGui::SameLine();

            ImGui::BeginGroup();
            if (item_selected_idx == 0) {
                if (ImGui::BeginTable("Shading", 2, ImGuiTableFlags_Borders)) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Shading type");
                    ImGui::TableSetColumnIndex(1);

                    std::vector<const char*> shading_items;
                    for (const auto& s : SHADING_TYPE_NAMES) {
                        shading_items.push_back(s.c_str());
                    }

                    int selected = int(Scene::graphics_settings().shading_type());

                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::
                            Combo("##shading_type", &selected, shading_items.data(), shading_items.size()))
                        Scene::set_shading_type(ShadingType(selected));

                    ImGui::EndTable();
                }

                ImGui::Separator();
                ImGui::PushFont(imgui_render.font(Render::ImguiFontType::Bold), GImGui->FontSizeBase);
                ImGui::Text("Shadows");
                ImGui::PopFont();
                ImGui::Separator();
                if (ImGui::BeginTable("Shadows", 2, ImGuiTableFlags_Borders)) {
                    int shadowsmap_size = Scene::graphics_settings().shadowsmap_size();
                    if (shadowsmap_size > 0) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("Shadowsmap size");
                        ImGui::TableSetColumnIndex(1);

                        const std::vector<int> sizes = {512, 1'024, 2'048, 4'096, 8'192};

                        std::vector<std::string> sizes_str;
                        std::transform(
                            sizes.begin(),
                            sizes.end(),
                            std::back_inserter(sizes_str),
                            [](int size)
                            { return std::to_string(size) + "x" + std::to_string(size); }
                        );

                        auto it = std::find(sizes.begin(), sizes.end(), shadowsmap_size);
                        DEBUG_ASSERT(it != sizes.end());
                        int sel_size = int(std::distance(sizes.begin(), it));

                        const char* preview_value = sizes_str[sel_size].c_str();

                        ImGui::SetNextItemWidth(items_width);
                        if (ImGui::BeginCombo("##sizes", preview_value)) {
                            for (int i = 0; i < int(sizes_str.size()); i++) {
                                bool is_selected = (sel_size == i);
                                if (ImGui::Selectable(sizes_str[i].c_str(), is_selected))
                                    sel_size = i;

                                if (is_selected)
                                    ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }

                        if (sizes[sel_size] != shadowsmap_size)
                            Scene::set_shadowsmap_size(sizes[sel_size]);
                    }

                    float intensity = Scene::graphics_settings().shadows_intensity();
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Intensity");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(items_width);
                    if (ImGui::SliderFloat("##intensity", &intensity, 0.2f, 1.0f, "%.2f", ImGuiSliderFlags_NoInput))
                        Scene::set_shadows_intensity(intensity);

                    ImGui::EndTable();
                }

                if (ImGui::Button("Default##Shadows"))
                    Scene::set_default_shadows_intensity();

                ImGui::Separator();
                ImGui::PushFont(imgui_render.font(Render::ImguiFontType::Bold), GImGui->FontSizeBase);
                ImGui::Text("Ambient occlusion");
                ImGui::PopFont();
                ImGui::Separator();

                Domain::Index2 fb_size = Scene::graphics_settings().ao_framebuffer_size();
                if (fb_size[0] > 0 && fb_size[1] > 0) {
                    if (ImGui::BeginTable("AO", 2, ImGuiTableFlags_Borders)) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("Framebuffer size");
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%dx%d", fb_size[0], fb_size[1]);

                        float intensity = Scene::graphics_settings().ao_intensity();
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("Intensity");
                        ImGui::TableSetColumnIndex(1);
                        ImGui::SetNextItemWidth(items_width);
                        if (ImGui::SliderFloat("##ao_intensity", &intensity, 0.1f, 5.0f, "%.1f", ImGuiSliderFlags_NoInput))
                            Scene::set_ao_intensity(intensity);

                        size_t k_size = Scene::graphics_settings().ao_kernel_size();
                        if (k_size > 0) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::AlignTextToFramePadding();
                            ImGui::Text("Kernel size");
                            ImGui::TableSetColumnIndex(1);

                            const std::vector<size_t> k_sizes = {
                                4,
                                8,
                                16,
                                32,
                                64,
                                128,
                                256,
                            };

                            std::vector<std::string> k_sizes_str;
                            std::transform(
                                k_sizes.begin(),
                                k_sizes.end(),
                                std::back_inserter(k_sizes_str),
                                [](size_t size) { return std::to_string(size); }
                            );

                            auto k_it = std::find(k_sizes.begin(), k_sizes.end(), k_size);
                            DEBUG_ASSERT(k_it != k_sizes.end());
                            size_t sel_k_size = size_t(std::distance(k_sizes.begin(), k_it));

                            const char* k_preview_value = k_sizes_str[sel_k_size].c_str();

                            ImGui::SetNextItemWidth(items_width);
                            if (ImGui::BeginCombo("##k_sizes", k_preview_value)) {
                                for (size_t i = 0; i < k_sizes_str.size(); i++) {
                                    bool is_selected = (sel_k_size == i);
                                    if (ImGui::Selectable(k_sizes_str[i].c_str(), is_selected))
                                        sel_k_size = i;

                                    if (is_selected)
                                        ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }

                            if (k_sizes[sel_k_size] != k_size)
                                Scene::set_ao_kernel_size(k_sizes[sel_k_size]);
                        }

                        size_t n_size = Scene::graphics_settings().ao_noise_size();
                        if (n_size > 0) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::AlignTextToFramePadding();
                            ImGui::Text("Noise size");
                            ImGui::TableSetColumnIndex(1);

                            const std::vector<size_t> n_sizes = {
                                4,
                                8,
                                16,
                                32,
                            };

                            std::vector<std::string> n_sizes_str;
                            std::transform(
                                n_sizes.begin(),
                                n_sizes.end(),
                                std::back_inserter(n_sizes_str),
                                [](size_t size)
                                { return std::to_string(size) + "x" + std::to_string(size); }
                            );

                            auto n_it = std::find(n_sizes.begin(), n_sizes.end(), n_size);
                            DEBUG_ASSERT(n_it != n_sizes.end());
                            size_t sel_n_size = size_t(std::distance(n_sizes.begin(), n_it));

                            const char* n_preview_value = n_sizes_str[sel_n_size].c_str();

                            ImGui::SetNextItemWidth(items_width);
                            if (ImGui::BeginCombo("##n_sizes", n_preview_value)) {
                                for (size_t i = 0; i < n_sizes_str.size(); i++) {
                                    bool is_selected = (sel_n_size == i);
                                    if (ImGui::Selectable(n_sizes_str[i].c_str(), is_selected))
                                        sel_n_size = i;

                                    if (is_selected)
                                        ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }

                            if (n_sizes[sel_n_size] != n_size)
                                Scene::set_ao_noise_size(n_sizes[sel_n_size]);
                        }

                        float radius = Scene::graphics_settings().ao_radius();
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("Radius");
                        ImGui::TableSetColumnIndex(1);
                        ImGui::SetNextItemWidth(items_width);
                        if (ImGui::SliderFloat("##ao_radius", &radius, 0.1f, 50.0f, "%.1f", ImGuiSliderFlags_NoInput))
                            Scene::set_ao_radius(radius);

                        float bias = Scene::graphics_settings().ao_bias();
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("Bias");
                        ImGui::TableSetColumnIndex(1);
                        ImGui::SetNextItemWidth(items_width);
                        if (ImGui::SliderFloat("##ao_bias", &bias, 0.001f, 10.0f, "%.3f", ImGuiSliderFlags_NoInput))
                            Scene::set_ao_bias(bias);

                        float z_threshold = Scene::graphics_settings().ao_z_threshold();
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("Z threshold");
                        ImGui::TableSetColumnIndex(1);
                        ImGui::SetNextItemWidth(items_width);
                        if (ImGui::SliderFloat("##ao_z_threshold", &z_threshold, 1.0f, 50.0f, "%.1f", ImGuiSliderFlags_NoInput))
                            Scene::set_ao_z_threshold(z_threshold);

                        size_t bf_size = Scene::graphics_settings().ao_blur_filter_size();
                        if (bf_size > 0) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::AlignTextToFramePadding();
                            ImGui::Text("Blur filter size");
                            ImGui::TableSetColumnIndex(1);

                            const std::vector<int> bf_sizes = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

                            std::vector<std::string> bf_sizes_str;
                            std::transform(
                                bf_sizes.begin(),
                                bf_sizes.end(),
                                std::back_inserter(bf_sizes_str),
                                [](int size)
                                { return std::to_string(size) + "x" + std::to_string(size); }
                            );

                            auto bf_it = std::find(bf_sizes.begin(), bf_sizes.end(), bf_size);
                            DEBUG_ASSERT(bf_it != bf_sizes.end());
                            int sel_bf_size = int(std::distance(bf_sizes.begin(), bf_it));

                            const char* bf_preview_value = bf_sizes_str[sel_bf_size].c_str();

                            ImGui::SetNextItemWidth(items_width);
                            if (ImGui::BeginCombo("##bf_sizes", bf_preview_value)) {
                                for (int i = 0; i < int(bf_sizes_str.size()); i++) {
                                    bool is_selected = (sel_bf_size == i);
                                    if (ImGui::Selectable(bf_sizes_str[i].c_str(), is_selected))
                                        sel_bf_size = i;

                                    if (is_selected)
                                        ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }

                            if (bf_sizes[sel_bf_size] != bf_size)
                                Scene::set_ao_blur_filter_size(bf_sizes[sel_bf_size]);
                        }

                        ImGui::EndTable();
                    }

                    if (ImGui::Button("Default##ao")) {
                        Scene::set_default_ao_intensity();
                        Scene::set_default_ao_kernel_size();
                        Scene::set_default_ao_noise_size();
                        Scene::set_default_ao_radius();
                        Scene::set_default_ao_bias();
                        Scene::set_default_ao_z_threshold();
                        Scene::set_default_ao_blur_filter_size();
                    }
                }

                ImGui::Separator();
                ImGui::PushFont(imgui_render.font(Render::ImguiFontType::Bold), GImGui->FontSizeBase);
                ImGui::Text("Physically based rendering");
                ImGui::PopFont();
                ImGui::Separator();

                if (ImGui::BeginTable("PBR", 2, ImGuiTableFlags_Borders)) {
                    float intensity = Scene::graphics_settings().pbr_intensity();
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Intensity");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(items_width);
                    if (ImGui::SliderFloat("##pbr_intensity", &intensity, 1.0f, 100.0f, "%.1f", ImGuiSliderFlags_NoInput))
                        Scene::set_pbr_intensity(intensity);

                    ImGui::EndTable();
                }

                if (ImGui::Button("Default##pbr"))
                    Scene::set_default_pbr_intensity();

                Node::NodeList nodes;
                scene_provider.scene().root().query(
                    [](const Node* n)
                    { return n->has_render_component() && n->render_component()->has_pbr(); },
                    nodes
                );

                if (!nodes.empty()) {
                    if (ImGui::BeginTable("PBR-scene", 5, ImGuiTableFlags_Borders)) {
                        ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
                        ImGui::TableSetupColumn("Node");
                        ImGui::TableSetupColumn("Metal");
                        ImGui::TableSetupColumn("Roughness");
                        ImGui::TableSetupColumn("IOR");
                        ImGui::TableHeadersRow();

                        for (size_t i = 0; i < nodes.size(); ++i) {
                            Node* n       = nodes[i];
                            PBRParams pbr = *n->render_component()->pbr();

                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::AlignTextToFramePadding();
                            std::string name = n->debug_name();
                            const SceneNodeTag* tag = n->tag_of_type<SceneNodeTag>();
                            if (tag != nullptr) {
                                name += " (";
                                name += std::to_string(tag->object_id);
                                name += ", " + std::to_string(tag->instance_id);
                                name += ", " + std::to_string(tag->volume_id);
                                name += ")";
                            }
                            ImGui::Text("%s", name.c_str());

                            ImGui::TableSetColumnIndex(1);
                            float metal = pbr.metal;
                            ImGui::SetNextItemWidth(items_width);
                            if (ImGui::SliderFloat(format("##metal%zu", i).c_str(), &metal, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_NoInput))
                                n->render_component()->set_pbr({metal, pbr.roughness, pbr.ior});

                            ImGui::TableSetColumnIndex(2);
                            float roughness = pbr.roughness;
                            ImGui::SetNextItemWidth(items_width);
                            if (ImGui::SliderFloat(format("##roughness%zu", i).c_str(), &roughness, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_NoInput))
                                n->render_component()->set_pbr({pbr.metal, roughness, pbr.ior});

                            ImGui::TableSetColumnIndex(3);
                            float ior = pbr.ior;
                            ImGui::SetNextItemWidth(items_width);
                            if (ImGui::SliderFloat(format("##ior%zu", i).c_str(), &ior, 1.0f, 2.5f, "%.3f", ImGuiSliderFlags_NoInput))
                                n->render_component()->set_pbr({pbr.metal, pbr.roughness, ior});

                            ImGui::TableSetColumnIndex(4);
                            if (ImGui::Button(format("Default##pbr%zu", i).c_str())) {
                                bool found = false;
                                const SceneNodeTag* sn_tag = n->tag_of_type<SceneNodeTag>();
                                if (sn_tag != nullptr) {
                                    pbr   = DEFAULT_VOLUME_PBRPARAMS;
                                    found = true;
                                } else {
                                    const BedNodeTag* bn_tag = n->tag_of_type<BedNodeTag>();
                                    if (bn_tag != nullptr) {
                                        pbr = (bn_tag->type == BedElementType::Model) ? DEFAULT_BED_MODEL_PBRPARAMS : DEFAULT_BED_PLATE_PBRPARAMS;
                                        found = true;
                                    } else {
                                        const libvgcode::GCodeNodeTag* gcn_tag = n->tag_of_type<libvgcode::GCodeNodeTag>(
                                        );
                                        if (gcn_tag != nullptr) {
                                            pbr = (gcn_tag->type == libvgcode::GCodeElementType::Toolpaths) ? DEFAULT_GCODE_SEGMENTS_PBRPARAMS : DEFAULT_GCODE_OPTIONS_PBRPARAMS;
                                            found = true;
                                        }
                                    }
                                }
                                if (found)
                                    n->render_component()->set_pbr(pbr);
                            }
                        }

                        ImGui::EndTable();
                    }
                }
            }  
            else if (item_selected_idx == 1) {
                Scene& scene    = scene_provider.scene();
                Lighting lights = scene.lights();

                float items_width  = 150.0f;
                bool modified      = false;
                static int edit_id = -1;

                bool pbr_enabled     = Scene::graphics_settings().pbr_enabled();
                bool shadows_enabled = Scene::graphics_settings().shadows_enabled();

                if (edit_id == -1) {
                    if (pbr_enabled) {
                        if (ImGui::BeginTable("Lighting options", 2, ImGuiTableFlags_Borders)) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::AlignTextToFramePadding();
                            ImGui::Text("Ambient intensity");
                            ImGui::TableSetColumnIndex(1);
                            ImGui::SetNextItemWidth(items_width);
                            if (ImGui::SliderFloat("##ambient", &lights.ambient_intensity, 0.1f, 5.0f, "%.2f", ImGuiSliderFlags_NoInput))
                                modified = true;

                            ImGui::EndTable();
                        }
                    }

                    int columns = pbr_enabled ? 7 : 10;
                    if (shadows_enabled)
                        ++columns;
                    if (ImGui::BeginTable("Lights", columns, ImGuiTableFlags_Borders)) {
                        ImGui::TableSetupColumn("Light");
                        ImGui::TableSetupColumn("Ref.System");
                        ImGui::TableSetupColumn("Azimuth");
                        ImGui::TableSetupColumn("Zenith");
                        if (!pbr_enabled)
                            ImGui::TableSetupColumn("Ambient");
                        ImGui::TableSetupColumn("Diffuse");
                        if (!pbr_enabled) {
                            ImGui::TableSetupColumn("Specular");
                            ImGui::TableSetupColumn("Shininess");
                        }
                        if (shadows_enabled)
                            ImGui::TableSetupColumn("Shadows");
                        ImGui::TableHeadersRow();

                        for (size_t i = 0; i < lights.lights.size(); ++i) {
                            int col_id = 0;
                            Light& l   = lights.lights[i];

                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(col_id++);
                            ImGui::AlignTextToFramePadding();
                            ImGui::Text("Light #%zu", i + 1);
                            ImGui::TableSetColumnIndex(col_id++);
                            ImGui::Text("%s", to_string(l.system).c_str());

                            std::pair<float, float> az = xyz_to_az(l.direction);
                            ImGui::TableSetColumnIndex(col_id++);
                            ImGui::Text("%.3f", rad2deg(az.first));
                            ImGui::TableSetColumnIndex(col_id++);
                            ImGui::Text("%.3f", rad2deg(az.second));
                            if (!pbr_enabled) {
                                ImGui::TableSetColumnIndex(col_id++);
                                ImGui::Text("%.3f", l.ambient);
                            }
                            ImGui::TableSetColumnIndex(col_id++);
                            ImGui::Text("%.3f", l.diffuse);
                            if (!pbr_enabled) {
                                ImGui::TableSetColumnIndex(col_id++);
                                ImGui::Text("%.3f", l.specular);
                                ImGui::TableSetColumnIndex(col_id++);
                                ImGui::Text("%.3f", l.shininess);
                            }

                            if (shadows_enabled) {
                                ImGui::TableSetColumnIndex(col_id++);
                                ImGui::Text("%s", l.shadows ? "Yes" : "No");
                            }

                            ImGui::TableSetColumnIndex(col_id++);
                            if (ImGui::Button(fmt::format("Edit##{}", i).c_str()))
                                edit_id = int(i);

                            bool remove_enabled = lights.lights.size() > 1;
                            if (!remove_enabled)
                                ImGui::BeginDisabled();
                            ImGui::TableSetColumnIndex(col_id++);
                            if (ImGui::Button(fmt::format("Remove##{}", i).c_str())) {
                                lights.lights.erase(lights.lights.begin() + i);
                                modified = true;
                            }
                            if (!remove_enabled)
                                ImGui::EndDisabled();
                        }

                        ImGui::EndTable();
                    }

                    bool add_enabled = lights.lights.size() < MAX_NUM_LIGHTS;
                    if (!add_enabled)
                        ImGui::BeginDisabled();

                    if (ImGui::Button("Add")) {
                        lights.lights.push_back(Light());
                        modified = true;
                    }

                    if (!add_enabled)
                        ImGui::EndDisabled();

                    ImGui::SameLine();
                    if (ImGui::Button("Default")) {
                        lights.lights            = DEFAULT_LIGHTS;
                        lights.ambient_intensity = DEFAULT_LIGHT_AMBIENT;
                        modified                 = true;
                    }

                    ImGui::Dummy({ 0.0f, 100.0f });
                } else {
                    ImGui::Text("Light #%d", edit_id + 1);

                    Light& l = lights.lights[edit_id];

                    if (ImGui::BeginTable("Light", 2, ImGuiTableFlags_Borders)) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("Reference system");
                        ImGui::TableSetColumnIndex(1);

                        std::vector<std::string> systems;
                        systems.reserve(LIGHT_REFERENCE_SYSTEMS_COUNT);
                        for (size_t i = 0; i < LIGHT_REFERENCE_SYSTEMS_COUNT; ++i) {
                            systems.emplace_back(to_string(LightReferenceSystem(i)));
                        }

                        const std::string& system = to_string(l.system);

                        auto it = std::find(systems.begin(), systems.end(), system);
                        DEBUG_ASSERT(it != systems.end());
                        int sel = int(std::distance(systems.begin(), it));

                        const char* preview_value = system.c_str();

                        ImGui::SetNextItemWidth(items_width);
                        if (ImGui::BeginCombo("##refsystem", preview_value)) {
                            for (int i = 0; i < int(systems.size()); i++) {
                                bool is_selected = (sel == i);
                                if (ImGui::Selectable(systems[i].c_str(), is_selected)) {
                                    sel = i;
                                    if (systems[sel] == "World") {
                                        l.direction = scene.camera().model().matrix().block<3, 3>(0, 0).cast<float>() * l.direction;
                                        DEBUG_ASSERT(std::abs(l.direction.norm() - 1.0f) < FLT_EPSILON);
                                    } else if (systems[sel] == "Camera") {
                                        l.direction = scene.camera().view().matrix().block<3, 3>(0, 0).cast<float>() * l.direction;
                                        DEBUG_ASSERT(std::abs(l.direction.norm() - 1.0f) < FLT_EPSILON);
                                    }
                                }

                                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                                if (is_selected)
                                    ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }

                        if (systems[sel] != system) {
                            l.system = LightReferenceSystem(sel);
                            modified = true;
                        }

                        std::pair<float, float> az = xyz_to_az(l.direction);
                        bool az_modified           = false;

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("Azimuth");
                        ImGui::TableSetColumnIndex(1);
                        ImGui::SetNextItemWidth(items_width);
                        ImGui::Text("0°");
                        ImGui::SameLine();
                        float azimuth = rad2deg(az.first);
                        if (ImGui::SliderFloat("##azimuth", &azimuth, 0.0f, 360.0f, "%.2f", ImGuiSliderFlags_NoInput))
                            az_modified = true;
                        ImGui::SameLine();
                        ImGui::Text("360°");

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("Zenith");
                        ImGui::TableSetColumnIndex(1);
                        ImGui::SetNextItemWidth(items_width);
                        ImGui::Text("0°");
                        ImGui::SameLine();
                        float zenith = rad2deg(az.second);
                        if (ImGui::SliderFloat("##zenith", &zenith, 0.0f, 180.0f, "%.2f", ImGuiSliderFlags_NoInput))
                            az_modified = true;
                        ImGui::SameLine();
                        ImGui::Text("180°");

                        if (az_modified) {
                            l.direction =
                                Biz::Algorithms::Geometry::spheric_to_dir(deg2rad(zenith), deg2rad(azimuth))
                                    .cast<float>();
                            modified = true;
                        }

                        if (!pbr_enabled) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::AlignTextToFramePadding();
                            ImGui::Text("Ambient");
                            ImGui::TableSetColumnIndex(1);
                            ImGui::SetNextItemWidth(items_width);
                            if (ImGui::SliderFloat("##ambient", &l.ambient, 0.1f, 5.0f, "%.2f", ImGuiSliderFlags_NoInput))
                                modified = true;
                        }

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("Diffuse");
                        ImGui::TableSetColumnIndex(1);
                        ImGui::SetNextItemWidth(items_width);
                        if (ImGui::SliderFloat("##diffuse", &l.diffuse, 0.0f, 5.0f, "%.2f", ImGuiSliderFlags_NoInput))
                            modified = true;

                        if (!pbr_enabled) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::AlignTextToFramePadding();
                            ImGui::Text("Specular");
                            ImGui::TableSetColumnIndex(1);
                            ImGui::SetNextItemWidth(items_width);
                            if (ImGui::SliderFloat("##specular", &l.specular, 0.0f, 5.0f, "%.2f", ImGuiSliderFlags_NoInput))
                                modified = true;

                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::AlignTextToFramePadding();
                            ImGui::Text("Shininess");
                            ImGui::TableSetColumnIndex(1);
                            ImGui::SetNextItemWidth(items_width);
                            if (ImGui::SliderFloat("##shininess", &l.shininess, 0.0f, 200.0f, "%.2f", ImGuiSliderFlags_NoInput))
                                modified = true;
                        }

                        if (shadows_enabled) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::AlignTextToFramePadding();
                            ImGui::Text("Cast shadows");
                            ImGui::TableSetColumnIndex(1);

                            if (ImGui::Checkbox("##shadows", &l.shadows)) {
                                if (l.shadows) {
                                    for (size_t i = 0; i < lights.lights.size(); ++i) {
                                        if (i == edit_id)
                                            continue;
                                        lights.lights[i].shadows = false;
                                    }
                                } else if (lights.lights.size() == 2) {
                                    for (size_t i = 0; i < lights.lights.size(); ++i) {
                                        if (i != edit_id)
                                            lights.lights[i].shadows = true;
                                    }
                                }
                                modified = true;
                            }
                        }

                        ImGui::EndTable();
                    }

                    ImGui::Separator();
                    if (ImGui::Button("Close")) {
                        edit_id    = -1;
                        reset_size = true;
                    }

                    ImGui::Dummy({ 0.0f, 100.0f });
                }

                if (modified) {
                    // ensure at least one light casts shadows
                    auto it = std::find_if(
                        lights.lights.begin(),
                        lights.lights.end(),
                        [](const Light& l) { return l.shadows; }
                    );
                    if (it == lights.lights.end())
                        lights.lights.front().shadows = true;

                    scene.set_lights(lights);
                }
            }
            else {
                if (ImGui::BeginTable("Bed options", 2, ImGuiTableFlags_Borders)) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Texture size");
                    ImGui::TableSetColumnIndex(1);

                    size_t texture_size = BedRenderHelper::bed_texture_size();
                    size_t max_texture_size = Render::Context::instance().max_texture_size() / 2;

                    std::vector<size_t> sizes;
                    for (size_t i = 512; i <= max_texture_size; i *= 2) {
                        sizes.push_back(i);
                    }

                    std::vector<std::string> sizes_str;
                    std::transform(sizes.begin(), sizes.end(), std::back_inserter(sizes_str),
                        [](size_t size) { return std::to_string(size) + "x" + std::to_string(size); }
                    );

                    auto it = std::find(sizes.begin(), sizes.end(), texture_size);
                    DEBUG_ASSERT(it != sizes.end());
                    int sel_size = int(std::distance(sizes.begin(), it));

                    const char* preview_value = sizes_str[sel_size].c_str();

                    ImGui::SetNextItemWidth(items_width);
                    if (ImGui::BeginCombo("##texture_sizes", preview_value)) {
                        for (int i = 0; i < int(sizes_str.size()); i++) {
                            bool is_selected = (sel_size == i);
                            if (ImGui::Selectable(sizes_str[i].c_str(), is_selected))
                                sel_size = i;

                            // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }

                    // force bed textures reload with the new size
                    if (sizes[sel_size] != texture_size) {
                        BedRenderHelper::set_bed_texture_size(sizes[sel_size]);
                        Scene& scene = scene_provider.scene();
                        visit(scene.root(), [&](Node& n) {
                                BedNodeTag* tag = n.tag_of_type<BedNodeTag>();
                                if (tag != nullptr && tag->type == BedElementType::PlateTextured) {
                                    const Domain::ConfigContainer* cc = project.find_config_container(tag->config_container_id);
                                    const Domain::BedInstance& inst = cc->find_bed_instance(tag->instance_id);
                                    n.render_component()->replace_material(BedMaterials::plate_textured_material(device, inst.bed.get()));
                                    if (n.has_material_override())
                                        n.set_material_override(BedMaterials::plate_textured_transparent_material(n.render_component()->material()));
                                }
                            }
                        );
                    }

                    ImGui::EndTable();
                }

                ImGui::Dummy({ 0.0f, 100.0f });
            }
            ImGui::EndGroup();
        }
    }
    ImGui::End();
}

} // namespace Slic3r::App::Scene
