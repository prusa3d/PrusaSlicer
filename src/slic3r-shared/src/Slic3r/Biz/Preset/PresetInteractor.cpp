#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

#include "spdlog/stopwatch.h"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Uuid.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Preset/HwConfigEvaluator.hpp"
#include "Slic3r/Biz/Preset/PresetEvaluator.hpp"
#include "Slic3r/Biz/Preset/IO/BundleLoader.hpp"
#include "Slic3r/Biz/Preset/IO/PresetSaver.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Biz/Preset/ProjectPresetView.hpp"
#include "Slic3r/Biz/Preset/PresetCollectionEvaluator.hpp"

#include "Slic3r/Biz/PrintToolConfigObservableList.hpp"
#include "Slic3r/Biz/OverridableConfigBoxObservableList.hpp"
#include "Slic3r/Biz/BackupStore.hpp"

#include "tbb/parallel_for.h"
#include "tbb/blocked_range.h"

#include "Slic3r/Version.hpp"
#include "Slic3r/Biz/Preset/IO/PresetLoader.hpp"

#include <vector>
#include <string>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem/path.hpp>
#include <Slic3r/Log.hpp>
#include <magic_enum/magic_enum.hpp>

using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec2ds;

namespace Slic3r::Biz::Preset {
namespace {

void dump_ep_info(const Domain::Preset::EvaluatedPrinterPreset& preset)
{
    SPDLOG_INFO("HwConfig: {} ({})", preset.hw_config.name, preset.hw_config.id);
    SPDLOG_INFO("Printer: {} ({})", preset.preset.name, preset.preset.id);
    for (const auto& p : preset.prints) {
        SPDLOG_INFO("-------------------------------------------------");
        SPDLOG_INFO("Print: {} ({})", p.preset.name, p.preset.id);
        size_t idx = 0;
        for (const auto& tvs : p.tools) {
            if (idx > 0)
                SPDLOG_INFO("................................................");
            idx++;
            SPDLOG_INFO("Tool {}", idx);
            for (const auto& t : tvs) {
                SPDLOG_INFO("- {} ({})", t.preset.name, t.preset.id);
            }

            size_t tool_idx = 0;
            SPDLOG_INFO("-------------------------------------------------");
            SPDLOG_INFO("Materials:");
            SPDLOG_INFO("-------------------------------------------------");
            for (const auto& tool_mats : p.materials) {
                if (tool_idx > 0)
                    SPDLOG_INFO("................................................");
                tool_idx++;
                SPDLOG_INFO("Tool {}", tool_idx);
                for (const auto& mat : tool_mats) {
                    SPDLOG_INFO("- {} ({})", mat.preset.name, mat.preset.id);
                }
            }
            SPDLOG_INFO("-------------------------------------------------");
        }
    }
}


void append_evaluated_presets_to_runtime(
    RuntimePresets& runtime_presets,
    const Domain::Preset::HwPrinterConfig& hw_config,
    const PresetEvaluator::EvaluatedPrinterPresets& epps
)
{
    auto& dest_printers = runtime_presets.printer[hw_config.id];
    for (const auto& epp : epps) {
        if (runtime_presets.find_printer_preset_by_id(hw_config.id, epp.preset.id) == nullptr) {
            dest_printers.push_back(epp.preset);
        }

        const RuntimePresets::HwConfigPrinterKey printer_key{hw_config.id, epp.preset.id};

        for (const auto& p : epp.prints) {
            if (runtime_presets.find_print_preset_by_id(printer_key, p.preset.id) == nullptr) {
                runtime_presets.print[printer_key].push_back(p.preset);
            }

            const RuntimePresets::HwConfingPrinterPrintKey print_key{
                hw_config.id,
                epp.preset.id,
                p.preset.id
            };

            size_t tool_idx = 0;
            for (const auto& tools : p.tools) {
                for (const auto& tool : tools) {
                    if (runtime_presets.find_tool_print_preset_by_id(print_key, tool_idx, tool.preset.id) != nullptr) {
                        continue;
                    }
                    runtime_presets.add_tool_print(print_key, hw_config, tool_idx, tool.preset);
                }
                tool_idx++;
            }

            size_t material_idx = 0;
            for (const auto& materials : p.materials) {
                for (const auto& material : materials) {
                    if (runtime_presets.find_material_preset_by_id(print_key, material_idx, material.preset.id) != nullptr) {
                        continue;
                    }
                    runtime_presets
                        .add_material(print_key, hw_config, material_idx, material.preset);
                }
                material_idx++;
            }
        }
    }
}


} // namespace

bool PresetItem::preset_runtime_only() const
{
    return origin == Domain::Preset::PresetOrigin::Runtime;
}

std::string PresetItem::ui_preset_name() const
{
    switch (origin) {
    case Domain::Preset::PresetOrigin::System:
        return name;

    case Domain::Preset::PresetOrigin::Runtime:
        //TRN {} is preset name
        return fmt::format(fmt::runtime(_u8L("(From 3mf) {}")), name);

    case Domain::Preset::PresetOrigin::User:
        //TRN {} is preset name
        return fmt::format(fmt::runtime(_u8L("(User) {}")), name);
    }

    UNREACHABLE();
    return "";
}

std::string PresetItem::ui_hw_config_name() const
{
    if (hw_printer_config_runtime_only) {
        //TRN {} is preset name
        return fmt::format(fmt::runtime(_u8L("(From 3mf) {}")), hw_printer_config_name);
    }
    return hw_printer_config_name;
}

PresetInteractor::PresetInteractor(
    Domain::Workbench& workbench,
    Scene::SceneInteractor& scene_interactor,
    BackupStore& backup_store
) :
    m_workbench(workbench),
    m_backup_store(backup_store),
    m_object_settings_interactor(
        m_object_settings_interactor_accessor,
        m_workbench,
        scene_interactor,
        *this
    ),
    m_print_tool_cbi(m_workbench, scene_interactor, m_print_tool_cbi_accessor)
{
    m_printer_cbi = ConfigBoxInteractor(m_printer_cbi_accessor);

    add_listener<IPresetChangedListener>(&m_object_settings_interactor);
}

void PresetInteractor::update_vendor_presets(std::mutex& mut, Domain::Preset::Bundle& preset_bundle, const std::string& vendor_id)
{
    spdlog::stopwatch sw;
    auto& mut_vendor_bundle = preset_bundle.vendor_bundles.at(vendor_id);

    auto upgrade_hw_config_features =
        [&preset_bundle, &vendor_id, this](Domain::Preset::HwPrinterConfig& hw_config)
    {
        if (hw_config.vendor_id == vendor_id) {
            auto result = update_hw_config_features(preset_bundle, hw_config);
            if (!result.has_value()) {
                SPDLOG_WARN(
                    "Error updating hw_config (name: {}, id: {}) features upgrade failed with error: {}",
                    hw_config.name,
                    hw_config.id,
                    result.error()
                );
            }
        }
    };

    for (auto& hw_config : preset_bundle.printer_configs | std::views::values) {
        upgrade_hw_config_features(hw_config);
    }

    for (auto& hw_config : mut_vendor_bundle.printer_configs) {
        upgrade_hw_config_features(hw_config);
    }

    for (auto& p : m_project_contexts | std::views::values) {
        for (auto& hw_config : p.runtime_presets.printer_configs | std::views::values) {
            upgrade_hw_config_features(hw_config);
        }

        if (p.invalid_hw_config.has_value()) {
            upgrade_hw_config_features(p.invalid_hw_config.value());
        }
    }

    for (auto& p : m_workbench.projects() | std::views::values) {
        for (auto& cc : p.config_containers()) {
            upgrade_hw_config_features(cc->mutable_selected_preset().hw_config);
        }
    }


    const auto& vendor_bundle = mut_vendor_bundle;

    // Update presets for global hw configs
    PresetEvaluator preset_evaluator{vendor_bundle.presets};
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, vendor_bundle.printer_configs.size()),
        [&](const tbb::blocked_range<size_t>& r) {
            for (size_t i = r.begin(); i != r.end(); ++i) {
                try {
                    const auto& hw_config = vendor_bundle.printer_configs[i];
                    auto epps = preset_evaluator.evaluate(hw_config, true);
                    {
                        std::lock_guard<std::mutex> guard(mut);
                        preset_bundle.evaluated_presets[hw_config.id] = std::move(epps);
                    }
                } catch (const std::exception& e) {
                    SPDLOG_ERROR("{}", e.what());
                }
            }
        }
    );

    // Update presets for runtime per-project hw configs
    std::vector<std::pair<size_t, std::string_view>> project_id_and_hw_config_id_pairs;
    for (auto& [p_id, ctx] : m_project_contexts) {
        for (const auto& hw_id: ctx.runtime_presets.printer_configs | std::views::keys) {
            project_id_and_hw_config_id_pairs.emplace_back(p_id, hw_id);
        }

        auto& rtp = ctx.runtime_presets;
        const auto pred = [](const auto& p){ return p.origin != Domain::Preset::PresetOrigin::Runtime; };
        const auto erase_non_runtime = [&pred]<typename RangeT>(RangeT&& range) {
            for (auto& ps : range) {
                std::erase_if(ps, pred);
            }
        };

        erase_non_runtime(rtp.printer | std::views::values);
        erase_non_runtime(rtp.print | std::views::values);
        erase_non_runtime(rtp.tool_print | std::views::values | std::views::join);
        erase_non_runtime(rtp.material | std::views::values | std::views::join);
    }

    tbb::parallel_for(tbb::blocked_range<size_t>(0, project_id_and_hw_config_id_pairs.size()), [&](const tbb::blocked_range<size_t>& range)
    {
        for (size_t i = range.begin(); i != range.end(); ++i) {
            size_t project_id;
            std::string_view hw_id;
            std::tie(project_id, hw_id) = project_id_and_hw_config_id_pairs.at(i);
            auto& ctx = m_project_contexts.at(project_id);
            const auto& hw_config = ctx.runtime_presets.printer_configs.at(std::string(hw_id));

            try {
                auto epps = preset_evaluator.evaluate(hw_config, true);
                {
                    std::lock_guard<std::mutex> guard(mut);
                    append_evaluated_presets_to_runtime(ctx.runtime_presets, hw_config, epps);
                }
            } catch (const std::exception& e) {
                SPDLOG_ERROR("{}", e.what());
            }
        }
    });

    SPDLOG_TRACE("Update vendor {} presets took {} secs", vendor_id, sw);

}

void PresetInteractor::load_preset_bundle(const IO::BundlePaths& bundle_paths)
{
    std::optional<Domain::Preset::Bundle> preset_bundle_opt;
#if !defined(NDEBUG) && !DEBUG_CONDITION_EVAL && SLIC3R_DEBUG_PRESET_CACHE
    namespace fs = boost::filesystem;
    const std::string bundle_cache_filename = fs::path(fs::path(Slic3r::data_dir()) / "cache" / "bundle_cache").string();
    preset_bundle_opt = IO::deserialize_bundle(bundle_cache_filename, bundle_paths, Slic3r::VERSION);
#endif
    if (! preset_bundle_opt) {
        if (bundle_paths.populate_local_bundle) {
            IO::populate_local_bundle(bundle_paths);
        }
        preset_bundle_opt = std::make_optional(IO::load_bundle(bundle_paths));
        Domain::Preset::Bundle& preset_bundle = *preset_bundle_opt;
        // copy over printer configs from original bundle, so these are not lost on reload
        if (m_workbench.has_preset_bundle()) {
            preset_bundle.printer_configs = m_workbench.preset_bundle().printer_configs;
            for (const auto& hw_config : preset_bundle.printer_configs | std::views::values) {
                auto& vendor_bundle = preset_bundle.vendor_bundles.at(hw_config.vendor_id);
                vendor_bundle.printer_configs.push_back(hw_config);
            }
        }

        // TODO: remove this when config wizard is ready
        {
            HwConfigEvaluator config_eval;
            for (const auto& vendor : {"PrusaResearch", "PrusaResearchSLA"}) {
                auto vendor_bundle_it = preset_bundle.vendor_bundles.find(vendor);
                ASSERT(vendor_bundle_it != preset_bundle.vendor_bundles.end() || strcmp(vendor, "PrusaResearch") != 0);
                if (vendor_bundle_it == preset_bundle.vendor_bundles.end()
                    || std::ranges::any_of(
                        preset_bundle.printer_configs | std::views::values,
                        [&](const auto& hw_config) { return hw_config.vendor_id == vendor; }
                    ))
                {
                    continue;
                }
                auto& vendor_bundle = vendor_bundle_it->second;
                for (const auto& hw_printer_template : vendor_bundle.vendor_data.printer_configs) {
                    auto printer_config = config_eval.create_printer_config(
                        hw_printer_template,
                        vendor_bundle.vendor_data
                    );
                    preset_bundle.printer_configs.emplace(printer_config.id, printer_config);
                    vendor_bundle.printer_configs.emplace_back(std::move(printer_config));
                }
            }
        }

        preset_bundle.evaluated_presets.clear();
        std::mutex mut;

        for (const auto& vendor_id : preset_bundle.vendor_bundles | std::views::keys) {
            update_vendor_presets(mut, preset_bundle, vendor_id);
        }
#if !defined(NDEBUG) && !DEBUG_CONDITION_EVAL && SLIC3R_DEBUG_PRESET_CACHE
        IO::serialize_bundle(bundle_cache_filename, *preset_bundle_opt, bundle_paths, Slic3r::VERSION);
#endif
    }

    m_bundle_paths = bundle_paths;

    // do not save it now, as we create it anyway again
    // IO::save_bundle_configs(preset_bundle, config_path);

    m_workbench.set_preset_bundle(std::move(*preset_bundle_opt));
    ListenerInvokeLaterBag bag;
    if (m_selected_project_id != Domain::INVALID_ID) {
        fill_printer_presets(false, bag);
        auto& selected_preset = mutable_selected_printer_preset();
        fill_tool_items(selected_preset.hw_config);
        update_print_tool_cbi(
            selected_preset,
            selected_config_container_context().config_container_id
        );
        fill_selected_material_cbis(selected_preset, true);
        bag.add([this]
        {
            invoke_listeners<IPresetChangedListener>(
                [](IPresetChangedListener* listener)
                {
                    listener->on_preset_bundles_loaded();
                }
            );
        });
    }
}

namespace {
template <typename Fdm, typename Sla>
std::string get_string(const Domain::Preset::EvaluatedPreset<Fdm, Sla>& p, const std::string& item_name)
{
    const auto* item = p.config_box().items.find(item_name);
    if (item == nullptr)
        return "";
    return item->value().template get<std::string>();
}

template <typename FdmConfigT, typename SlaConfigT>
const std::string* get_system_counterpart_id(
    const Domain::Preset::EvaluatedPreset<FdmConfigT, SlaConfigT>& preset
)
{
    auto it = preset.features.find(IO::FEATURE_BASED_ID);
    if (it != preset.features.end()) {
        return &std::get<std::string>(it->second);
    }
    if (preset.origin == Domain::Preset::PresetOrigin::System) {
        return &preset.id;
    }

    return nullptr;
}

template <typename FdmConfigT, typename SlaConfigT>
const Domain::Preset::EvaluatedPreset<FdmConfigT, SlaConfigT>& get_system_counterpart(
    const Domain::Preset::EvaluatedPreset<FdmConfigT, SlaConfigT>& preset,
    std::function<const Domain::Preset::EvaluatedPreset<FdmConfigT, SlaConfigT>(const std::string& id)>& preset_getter
)
{
    return preset_getter(get_system_counterpart_id(preset));
}

template <typename FdmConfigT, typename SlaConfigT>
std::vector<std::string> save_preset(
    const IO::BundlePaths& paths,
    const std::string& vendor_id,
    const std::string& repo_id,
    Domain::Preset::Bundle& bundle,
    Domain::Preset::EvaluatedPreset<FdmConfigT, SlaConfigT>& t,
    const Domain::Preset::EvaluatedPreset<FdmConfigT, SlaConfigT>* system,
    const KeySet& item_names_to_omit,
    const Domain::Preset::PresetNames& names,
    std::string&& new_name
)
{

    const bool needs_copy = t.origin != Domain::Preset::PresetOrigin::User;
    const bool is_new =
        std::ranges::none_of(names, [&](const auto& name) { return name.name == new_name; });
    std::vector<std::string> ids_to_detach;
    std::set<std::string> root_ids_to_detach;
    if (!new_name.empty()) {
        auto it = std::ranges::find_if(
            names,
            [&](const auto& name) { return name.name == new_name && (!name.id.contains(t.id)); }
        );
        if (it != names.end()) {
            ids_to_detach = std::vector<std::string>(it->id.begin(), it->id.end());
            root_ids_to_detach = it->root_id;
        }
        t.name = std::move(new_name);
    } else {
        ASSERT(!needs_copy);
    }

    const auto original_id = t.id;
    if (needs_copy) {
        if (t.origin == Domain::Preset::PresetOrigin::System) {
            t.features[IO::FEATURE_BASED_ID] = t.id;
            t.features[IO::FEATURE_BASED_ROOT_ID] = t.root_id;
        }
        t.id = generate_uuid();
        t.root_id = generate_uuid();
        t.origin = Domain::Preset::PresetOrigin::User;
    } else if (is_new) {
        t.root_id = generate_uuid();
        t.id = generate_uuid();
    }

    auto path = IO::preset_path(paths, t.kind, t.name, vendor_id, repo_id);
    auto node = IO::transform_for_saving(t, system, item_names_to_omit);
    IO::save_transformed_preset_as_user(node, path.string());

    auto& vendor_bundle = bundle.vendor_bundles.at(vendor_id);
    auto& nodes = vendor_bundle.presets.at(t.kind);

    auto it = std::ranges::find_if(
        nodes,
        [&](auto& n) { return n.id == node.id || root_ids_to_detach.contains(n.id); }
    );
    if(it != nodes.end()) {
        *it = node;
    } else {
        nodes.push_back(node);
    }
    return ids_to_detach;
}

SelectedPresetIds from_selected_preset(const Domain::Preset::SelectedPreset& sp)
{
    SelectedPresetIds ret = {
        .hw_config_id = sp.hw_config.id,
        .repo_id = sp.hw_config.repo_id,
        .vendor_id = sp.hw_config.vendor_id,
        .printer_id =  sp.printer.id,
        .print_id = sp.print.id
    };

    ret.tool_ids.reserve(sp.tools.size());
    std::ranges::copy(
        sp.tools | std::ranges::views::transform([](const auto& ep) { return ep.id; }),
        std::back_inserter(ret.tool_ids)
    );

    ret.material_ids.reserve(sp.materials.size());
    std::ranges::copy(
        sp.materials | std::ranges::views::transform([](const auto& ep) { return ep.id; }),
        std::back_inserter(ret.material_ids)
    );

    return ret;
}

std::tuple<const std::string&, const std::string&> sort_key(const PresetItem& item)
{
    return std::tie(item.name, item.hw_printer_config_name);
}

void emplace_item(
    std::vector<PresetItem>& items,
    const std::string& id,
    std::string name,
    Domain::Preset::PresetOrigin origin,
    const std::string& hw_config_id,
    const std::string& hw_config_name,
    bool hw_config_runtime
)
{
    items.emplace_back(
        id,
        std::move(name),
        hw_config_id,
        hw_config_name,
        origin,
        hw_config_runtime
    );
}

} // namespace

void PresetInteractor::save_user_preset(
    Domain::Preset::PresetKind kind,
    size_t slot_index
)
{
    InvokeLaterBag bag;
    ASSERT(m_dialog_manager != nullptr);
    const auto& sel_pres = selected_printer_preset();
    std::optional<std::string> preset_name;
    switch (kind) {
    case Domain::Preset::PresetKind::FdmPrinter:
    case Domain::Preset::PresetKind::SlaPrinter:
        kind = Domain::Preset::printer_kind(sel_pres.hw_config.technology);
        preset_name = sel_pres.printer.short_name();
        break;
    case Domain::Preset::PresetKind::FdmPrint:
    case Domain::Preset::PresetKind::SlaPrint:
        kind = Domain::Preset::print_kind(sel_pres.hw_config.technology);
        preset_name = sel_pres.print.short_name();
        break;
    case Domain::Preset::PresetKind::FdmToolPrint:
    case Domain::Preset::PresetKind::SlaToolPrint:
        kind = Domain::Preset::tool_print_kind(sel_pres.hw_config.technology);
        preset_name = sel_pres.tools.at(slot_index).short_name();
        break;
    case Domain::Preset::PresetKind::FdmMaterial:
    case Domain::Preset::PresetKind::SlaMaterial:
        kind = Domain::Preset::material_kind(sel_pres.hw_config.technology);
        preset_name = sel_pres.materials.at(slot_index).short_name();
        break;
    }
    m_unsaved_changes_selected_ids = from_selected_preset(selected_printer_preset());
    ASSERT(m_dialog_manager != nullptr);
    auto new_name = m_dialog_manager->show_save_dialog(kind, preset_name.value(), *this);

    if (new_name.empty()) {
        // cancel was pressed
        return;
    }
    save_user_preset_internal(kind, slot_index, {}, std::move(new_name), bag);
}

void PresetInteractor::save_user_tool_print_presets()
{
    InvokeLaterBag bag;
    const auto& selected_preset = selected_printer_preset();
    ASSERT(selected_preset.technology() == Domain::PrinterTechnology::FFF);

    using namespace Domain::Preset;
    const size_t tools_cnt = selected_preset.tools.size();
    if (tools_cnt == 1) {
        save_user_preset(PresetKind::FdmPrint, 0);
        return;
    }

    IPresetDialogManager::NamesPerKindMap original_names_per_kind = {
        {PresetKind::FdmPrint, {std::string{selected_preset.print.short_name()}}},
        {PresetKind::FdmToolPrint, {std::string{selected_preset.tools.at(0).short_name()}}}
    };
    for (size_t slot_index{1}; slot_index < tools_cnt; slot_index++) {
        original_names_per_kind.at(PresetKind::FdmToolPrint)
            .emplace_back(selected_preset.tools.at(slot_index).short_name());
    };

    m_unsaved_changes_selected_ids = from_selected_preset(selected_printer_preset());
    ASSERT(m_dialog_manager != nullptr);
    IPresetDialogManager::NamesPerKindMap new_names =
        m_dialog_manager->show_save_print_tool_dialog(original_names_per_kind, *this);

    if (new_names.empty()) {
        // cancel was pressed
        return;
    }

    if (!new_names.at(PresetKind::FdmPrint).front().empty()) {
        save_user_preset_internal(
            PresetKind::FdmPrint,
            0,
            {},
            std::move(new_names.at(PresetKind::FdmPrint).front()),
            bag
        );
    }

    for (size_t slot_index{}; slot_index < tools_cnt; slot_index++) {
        if (new_names.at(PresetKind::FdmToolPrint).at(slot_index).empty())
            continue;
        save_user_preset_internal(
            PresetKind::FdmToolPrint,
            slot_index,
            {},
            new_names.at(PresetKind::FdmToolPrint).at(slot_index),
            bag
        );
    };
}

void PresetInteractor::save_user_preset_internal(
    Domain::Preset::PresetKind kind,
    size_t slot_index,
    const KeySet& item_names_to_omit,
    std::string new_name,
    InvokeLaterBag& bag
)
{
    auto& selected_preset = mutable_selected_printer_preset();
    const auto& ids = m_unsaved_changes_selected_ids;
    auto& preset_bundle   = m_workbench.preset_bundle();
    auto names = get_all_vendor_preset_names(kind, ids.vendor_id);
    std::vector<std::string> ids_to_delete;
    Domain::Preset::PresetOrigin source_origin;

    switch (kind) {
    case Domain::Preset::PresetKind::FdmPrinter:
    case Domain::Preset::PresetKind::SlaPrinter:
        source_origin = selected_preset.printer.origin;
        ids_to_delete = save_preset(
            m_bundle_paths,
            ids.vendor_id,
            ids.repo_id,
            preset_bundle,
            selected_preset.printer,
            get_printer_system_preset(
                m_selected_project_id,
                ids.hw_config_id,
                ids.printer_id
            ),
            item_names_to_omit,
            names,
            std::move(new_name)
        );
        break;

    case Domain::Preset::PresetKind::FdmPrint:
    case Domain::Preset::PresetKind::SlaPrint:
        source_origin = selected_preset.print.origin;
        ids_to_delete = save_preset(
            m_bundle_paths,
            ids.vendor_id,
            ids.repo_id,
            preset_bundle,
            selected_preset.print,
            get_print_system_preset(
                m_selected_project_id,
                ids.hw_config_id,
                ids.printer_id,
                ids.print_id
            ),
            item_names_to_omit,
            names,
            std::move(new_name)
        );
        break;

    case Domain::Preset::PresetKind::FdmToolPrint:
    case Domain::Preset::PresetKind::SlaToolPrint:
        source_origin = selected_preset.tools.at(slot_index).origin;
        ids_to_delete = save_preset(
            m_bundle_paths,
            ids.vendor_id,
            ids.repo_id,
            preset_bundle,
            selected_preset.tools[slot_index],
            get_tool_print_system_preset(
                m_selected_project_id,
                ids.hw_config_id,
                ids.printer_id,
                ids.print_id,
                slot_index,
                ids.tool_ids[slot_index]
            ),
            item_names_to_omit,
            names,
            std::move(new_name)
        );
        break;

    case Domain::Preset::PresetKind::FdmMaterial:
    case Domain::Preset::PresetKind::SlaMaterial:
        source_origin = selected_preset.materials.at(slot_index).origin;
        ids_to_delete = save_preset(
            m_bundle_paths,
            ids.vendor_id,
            ids.repo_id,
            preset_bundle,
            selected_preset.materials[slot_index],
            get_material_system_preset(
                m_selected_project_id,
                ids.hw_config_id,
                ids.printer_id,
                ids.print_id,
                slot_index,
                ids.material_ids[slot_index]
            ),
            item_names_to_omit,
            names,
            std::move(new_name)
        );
        break;
    }

    for (const auto& id_to_delete : ids_to_delete) {
        delete_preset(kind, id_to_delete);
    }


    bag.add(
        [=, this, &selected_preset, vendor_id = ids.vendor_id]
        {
            reload_vendor_presets(vendor_id);

            const bool is_runtime = source_origin == Domain::Preset::PresetOrigin::Runtime;

            InvokeLaterBag inner_bag;
            switch (kind) {
            case Domain::Preset::PresetKind::FdmPrinter:
            case Domain::Preset::PresetKind::SlaPrinter:
                fill_printer_presets(false, inner_bag);
                if (is_runtime) {
                    // For runtime preset being saved we need to reselect the saved user printer
                    // preset to update it donwstream presets (print, tool and material) as these
                    // are distnict to runtime ones
                    select_printer_preset(selected_preset.hw_config.id, selected_preset.printer.id);
                } else {
                    // After reloading vendor presets, the original presets may be reorganized,
                    // invalidating the cached original configurations.
                    // Update the configurations in the ConfigBoxInteractors.
                    fill_selected_material_cbis(selected_preset, true);
                }
                break;

            case Domain::Preset::PresetKind::FdmPrint:
            case Domain::Preset::PresetKind::SlaPrint:
                fill_print_presets(selected_preset, false, inner_bag);
                if (is_runtime) {
                    // For runtime preset being saved we need to reselect the saved user print
                    // preset to update it donwstream presets (tool and material) as these
                    // are distnict to runtime ones
                    select_print_preset(selected_preset.print.id);
                }
                break;

            case Domain::Preset::PresetKind::FdmToolPrint:
            case Domain::Preset::PresetKind::SlaToolPrint:
                fill_tools_presets(selected_preset, false, inner_bag);
                break;

            case Domain::Preset::PresetKind::FdmMaterial:
            case Domain::Preset::PresetKind::SlaMaterial:
                fill_materials_presets(selected_preset, false, inner_bag);
                break;
            }
        }, "save_user_preset_internal"
    );
}

void PresetInteractor::reload_vendor_presets(const std::string& vendor_id)
{
    std::mutex mut;
    auto& preset_bundle = m_workbench.preset_bundle();
    update_vendor_presets(mut, preset_bundle, vendor_id);
    Domain::Preset::VendorBundle& vendor_bundle = preset_bundle.vendor_bundles.at(vendor_id);
    vendor_bundle.preset_names = IO::collect_names(vendor_bundle.presets);
}

const PresetInteractorConfigContainerContext& PresetInteractor::config_container_context(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id
) const
{
    auto it = get_project_context(project_id);
    ASSERT(it != m_project_contexts.end());
    const auto& project_ctx = it->second;
    const auto& cccs        = project_ctx.config_containers;
    return cccs.find(config_container_id)->second;
}

const PresetInteractorConfigContainerContext&
PresetInteractor::selected_config_container_context() const
{
    auto it = get_project_context(m_selected_project_id);
    ASSERT(it != m_project_contexts.end());
    const auto& project_ctx = it->second;
    const auto& cccs        = project_ctx.config_containers;
    ASSERT(project_ctx.selected_config_container_id != Domain::INVALID_ID);
    auto cc_it = cccs.find(project_ctx.selected_config_container_id);
    ASSERT(cc_it != cccs.end());
    return cc_it->second;
}

PresetInteractorConfigContainerContext& PresetInteractor::initialize_config_container_context(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id
)
{
    auto& project_context{get_or_create_project_context(project_id)};
    PresetInteractorConfigContainerContext ccc{config_container_id};
    const auto [it, inserted]{
        project_context.config_containers.insert({config_container_id, std::move(ccc)})
    };
    ASSERT(inserted);
    return it->second;
}

void PresetInteractor::initialize_config_container_with_default(Domain::ConfigContainer& cc)
{
    const static std::string selected_printer_name =
        "Prusa CORE One"; //"SL1S SPEED";//"Prusa MK4S";
    const auto& preset_bundle     = m_workbench.preset_bundle();

    std::vector<PresetItem> items;
    for (const auto& p : preset_bundle.evaluated_presets | std::views::values | std::views::join) {

        emplace_item(
            items,
            p.preset.id,
            p.preset.name,
            p.preset.origin,
            p.hw_config.id,
            m_use_hw_config_short_name ? p.hw_config.short_name : p.hw_config.name,
            false
        );
    }

    ASSERT(items.size() > 0);

    auto filter_pred = [](const PresetItem& p)
    {
        return p.hw_printer_config_name.starts_with(selected_printer_name)
            && p.origin == Domain::Preset::PresetOrigin::System;
    };
    auto filtered_range = items | std::views::filter(filter_pred);
    auto it = std::ranges::min_element(filtered_range, {}, sort_key);
    const PresetItem p  = it == std::ranges::end(filtered_range) ? items.front() : *it;

    InvokeLaterBag bag;
    fill_config_container_with_selected_preset(cc, p.hw_printer_config_id, p.id, false, bag);
}

void PresetInteractor::initialize_config_container_with_selected(Domain::ConfigContainer& cc)
{
    auto& selected_preset = cc.mutable_selected_preset();
    selected_preset = this->selected_printer_preset();
}

void PresetInteractor::on_selected_project_changed(size_t index)
{
    m_selected_project_id = index;
    // ensure the project context is created
    get_or_create_project_context(m_selected_project_id);
}


void PresetInteractor::on_selected_config_container_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId container_id
)
{
    m_selected_project_id                                                  = project_id;
    get_or_create_project_context(project_id).selected_config_container_id = container_id;

    // update selected config
    get_or_create_config_container_context(m_selected_project_id, container_id);
    auto& p = get_or_create_project_context(m_selected_project_id);
    p.invalid_hw_config = std::nullopt;

    ListenerInvokeLaterBag bag;

    fill_printer_presets(true, bag);
    Domain::Preset::SelectedPreset& selected_preset = mutable_selected_printer_preset();

    fill_tool_items(selected_preset.hw_config);
    fill_sheet_items(selected_preset.hw_config);

    fill_print_presets(selected_preset, true, bag);
    fill_tools_presets(selected_preset, true, bag);
    fill_materials_presets(selected_preset, true, bag);

    update_print_tool_cbi(selected_preset, container_id);

    bag.add([this, project_id, container_id]{
        // notify listeners on changes
        invoke_listeners<IPresetChangedListener>(
            [&](auto* l) { l->on_config_container_selection_changed(project_id, container_id); }
            );
    });
    bag.add([this] { invoke_slicing_input_changed(); });
}

bool PresetInteractor::update_changed_printer_configs(
    const std::vector<Domain::Preset::HwPrinterConfig>& hw_configs)
{
    auto& preset_bundle{m_workbench.preset_bundle()};

    const std::string& selected_printer_root_id{selected_printer_preset().printer.root_id};

    ListenerInvokeLaterBag bag;

    std::vector<PresetEvaluator::EvaluatedPrinterPresets> evaluated_presets;
    evaluated_presets.resize(hw_configs.size());

    tbb::parallel_for( //
        tbb::blocked_range<size_t>(0, hw_configs.size()),
        [&preset_bundle, &evaluated_presets, &hw_configs](const tbb::blocked_range<size_t>& range)
        {
            for (std::size_t index{range.begin()}; index < range.end(); ++index) {
                const Domain::Preset::HwPrinterConfig& hw_config{hw_configs[index]};
                const auto& vendor_bundle{preset_bundle.vendor_bundles.at(hw_config.vendor_id)};
                PresetEvaluator preset_evaluator{vendor_bundle.presets};
                PresetEvaluator::EvaluatedPrinterPresets& evaluated_printer_presets{
                    evaluated_presets.at(index)};
                try {
                    evaluated_printer_presets = preset_evaluator.evaluate(hw_config);
                } catch (const std::exception& e) {
                    SPDLOG_ERROR("{}", e.what());
                }

                std::erase_if(evaluated_printer_presets,
                              [](const Domain::Preset::EvaluatedPrinterPreset& epp)
                              { return epp.prints.empty(); });
            }
        });

    std::set<std::string> vendor_ids;
    for (const Domain::Preset::HwPrinterConfig& config : hw_configs) {
        vendor_ids.insert(config.vendor_id);
    }
    for (const std::string& vendor_id : vendor_ids) {
        auto& vendor_bundle{preset_bundle.vendor_bundles.at(vendor_id)};

        for (std::size_t index{}; index < hw_configs.size(); ++index) {
            const Domain::Preset::HwPrinterConfig& hw_config{hw_configs[index]};
            if (hw_config.vendor_id != vendor_id) {
                continue;
            }
            auto& evaluated_printer_presets{evaluated_presets.at(index)};
            if (evaluated_printer_presets.empty()) {
                continue;
            }
            preset_bundle.evaluated_presets[hw_config.id] = std::move(evaluated_printer_presets);
            preset_bundle.printer_configs[hw_config.id] = hw_config;

            auto* vb_config{vendor_bundle.find_printer_config(hw_config.id)};
            if (vb_config == nullptr) {
                vendor_bundle.printer_configs.push_back(hw_config);
            } else {
                *vb_config = hw_config;
            }
        }
    }

    std::string selected_hw_config_id{};
    std::string selected_printer_preset_id{};

    for (const Domain::Preset::HwPrinterConfig& hw_config : hw_configs) {
        auto& evaluated_printer_presets{preset_bundle.evaluated_presets[hw_config.id]};
        for (const auto& evaluated_printer_preset : evaluated_printer_presets) {
            if (evaluated_printer_preset.preset.root_id == selected_printer_root_id) {
                selected_hw_config_id = hw_config.id;
                selected_printer_preset_id = evaluated_printer_preset.preset.id;
            }
        }
    }

    fill_printer_presets(false, bag);

    if (!selected_hw_config_id.empty()) {
        ASSERT(!selected_printer_preset_id.empty());
        select_printer_preset_internal(selected_hw_config_id,
                                       selected_printer_preset_id,
                                       false,
                                       bag);
    }

    return true;
}

bool PresetInteractor::update_changed_selected_preset_hw_config(Domain::Preset::HwPrinterConfig& hw_config)
{
    auto& p = get_or_fail_project_context(m_selected_project_id);
    const std::string& printer_root_id = selected_printer_preset().printer.root_id;
    std::string selected_printer_preset_id;
    auto& preset_bundle = m_workbench.preset_bundle();
    auto& vendor_bundle = preset_bundle.vendor_bundles.at(hw_config.vendor_id);
    PresetEvaluator preset_evaluator{vendor_bundle.presets};
    PresetEvaluator::EvaluatedPrinterPresets epps;
    try {
        epps = preset_evaluator.evaluate(hw_config);
        std::erase_if(
            epps,
            [](const Domain::Preset::EvaluatedPrinterPreset& epp) { return epp.prints.empty(); }
        );

        if (epps.empty()) {
            p.invalid_hw_config = hw_config;
            return false;
        }

        p.invalid_hw_config = std::nullopt;

        for (auto& epp : epps) {
            if (epp.preset.root_id == printer_root_id)
                selected_printer_preset_id = epp.preset.id;
        }
        // we didn't find printer with same root_id, let's just select first one
        if (selected_printer_preset_id.empty())
            selected_printer_preset_id = epps.front().preset.id;

    } catch (const std::exception& e) {
        SPDLOG_ERROR("{}", e.what());
    }

    ListenerInvokeLaterBag bag;

    auto& project = m_workbench.project(m_selected_project_id);
    const auto& ccc     = selected_config_container_context();
    auto* cc            = project.find_config_container(ccc.config_container_id);
    ASSERT(cc != nullptr);
    duplicate_hw_config_if_needed_and_update(hw_config, bag);

    auto* vb_config     = vendor_bundle.find_printer_config(hw_config.id);
    if (vb_config == nullptr)
        vendor_bundle.printer_configs.push_back(hw_config);
    else
        *vb_config = hw_config;

    auto& evaluated_printer_presets = preset_bundle.evaluated_presets[hw_config.id];
    ASSERT(epps.size() > 0);
    evaluated_printer_presets = epps;

    fill_printer_presets(false, bag);
    select_printer_preset_internal(hw_config.id, selected_printer_preset_id, false, bag);

    return true;
}

const std::string& PresetInteractor::selected_hw_config_id() const
{
    const auto& project = m_workbench.project(m_selected_project_id);
    const auto& ccc     = selected_config_container_context();
    const auto* cc      = project.find_config_container(ccc.config_container_id);
    ASSERT(cc != nullptr);
    return cc->selected_preset().hw_config.id;
}

const Domain::Preset::HwPrinterConfig& PresetInteractor::current_printer_config() const
{
    return get_printer_config((selected_hw_config_id())).first.get();
}

const Domain::Preset::EvaluatedPrinterPreset::Preset&
PresetInteractor::current_printer_preset() const
{
    const auto& project = m_workbench.project(m_selected_project_id);
    const auto& ccc     = selected_config_container_context();
    const auto* cc      = project.find_config_container(ccc.config_container_id);
    ASSERT(cc != nullptr);

    const auto& selected_preset = cc->selected_preset();
    const auto& hw_config_id    = selected_preset.hw_config.id;
    const auto& printer_id      = selected_preset.printer.id;
    return get_printer_preset(hw_config_id, printer_id).first.get();
}

const Domain::Preset::SelectedPreset& PresetInteractor::selected_printer_preset() const
{
    const auto& project = m_workbench.project(m_selected_project_id);
    const auto& ccc     = selected_config_container_context();
    const auto* cc      = project.find_config_container(ccc.config_container_id);
    return cc->selected_preset();
}

bool PresetInteractor::is_printer_preset_selected_and_dirty(
    const std::string& hw_config_id,
    const std::string& printer_preset_id
) const
{
    const auto& selected_preset = selected_printer_preset();
    if (selected_preset.hw_config.id != hw_config_id
        || selected_preset.printer.id != printer_preset_id)
    {
        return false;
    }
    return m_printer_cbi.config_box_list().lock()->is_dirty();
}

bool PresetInteractor::is_print_preset_selected_and_dirty(
    const std::string& hw_config_id,
    const std::string& printer_preset_id,
    const std::string& print_preset_id
) const
{
    const auto& selected_preset = selected_printer_preset();
    if (selected_preset.hw_config.id != hw_config_id
        || selected_preset.printer.id != printer_preset_id
        || selected_preset.print.id != print_preset_id)
    {
        return false;
    }
    return m_print_tool_cbi.observable_list().lock()->is_dirty_print();
}

bool PresetInteractor::is_tool_print_preset_selected_and_dirty(
    const std::string& hw_config_id,
    const std::string& printer_preset_id,
    const std::string& print_preset_id,
    const std::string& tool_print_preset_id,
    size_t tool_index
) const
{
    const auto& selected_preset = selected_printer_preset();
    if (selected_preset.hw_config.id != hw_config_id
        || selected_preset.printer.id != printer_preset_id
        || selected_preset.print.id != print_preset_id
        || selected_preset.tools[tool_index].id != tool_print_preset_id)
    {
        return false;
    }
    return m_print_tool_cbi.observable_list().lock()->is_dirty_tool(tool_index);
}

bool PresetInteractor::is_tool_material_preset_selected_and_dirty(
    const std::string& hw_config_id,
    const std::string& printer_preset_id,
    const std::string& print_preset_id,
    const std::string& tool_material_preset_id,
    size_t slot_index
) const
{
    const auto& selected_preset = selected_printer_preset();
    if (selected_preset.hw_config.id != hw_config_id
        || selected_preset.printer.id != printer_preset_id
        || selected_preset.print.id != print_preset_id
        || selected_preset.materials[slot_index].id != tool_material_preset_id)
    {
        return false;
    }
    if (slot_index >= m_material_cbi_list.size())
        return false;
    return m_material_cbi_list.at(slot_index).config_box_overridable_list().lock()->is_dirty();
}

void PresetInteractor::discard_selected_printer_preset_changes()
{
    auto& selected_preset = mutable_selected_printer_preset();
    std::vector<Domain::ConfigItem>& all_items =
        selected_preset.printer.config_box().items.all_items();

    for (Domain::ConfigItem& item : all_items) {
        if (m_printer_cbi.config_box_list().lock()->is_dirty(item.name()))
            set_from_original_value(item);
    }
}

void PresetInteractor::discard_selected_print_preset_changes()
{
    auto& selected_preset = mutable_selected_printer_preset();
    std::vector<Domain::ConfigItem>& all_items =
        selected_preset.print.config_box().items.all_items();

    for (Domain::ConfigItem& item : all_items) {
        if (m_print_tool_cbi.observable_list().lock()->is_dirty_print(item.name()))
            set_from_original_value(item);
    }
}

void PresetInteractor::discard_selected_tool_print_preset_changes(size_t tool_index)
{
    auto& selected_preset = mutable_selected_printer_preset();
    std::vector<Domain::ConfigItem>& all_items =
        selected_preset.tools.at(tool_index).config_box().overrides.all_items();

    for (Domain::ConfigItem& item : all_items) {
        if (m_print_tool_cbi.observable_list().lock()->is_dirty_tool(item.name(), tool_index))
            set_from_original_value(item, tool_index);
    }
}

void PresetInteractor::discard_selected_tool_material_preset_changes(size_t slot_index)
{
    auto& selected_preset         = mutable_selected_printer_preset();
    Domain::ConfigBox& config_box = selected_preset.materials[slot_index].config_box();

    for (Domain::ConfigItem& item : config_box.items.all_items()) {
        if (m_material_cbi_list.at(slot_index)
                .config_box_overridable_list()
                .lock()
                ->is_dirty(item.name()))
            set_from_original_value(item, slot_index);
    }
    for (Domain::ConfigItem& item : config_box.overrides.all_items()) {
        if (m_material_cbi_list.at(slot_index)
                .config_box_overridable_list()
                .lock()
                ->is_dirty(item.name()))
            set_from_original_value(item, slot_index);
    }
}

void PresetInteractor::set_unsaved_changes(
    PresetsSwitchStates&& unsaved_changes
)
{
    m_unsaved_changes = unsaved_changes;
    m_unsaved_changes_selected_ids = from_selected_preset(selected_printer_preset());
}

Vec2ds PresetInteractor::system_preset_bed_shape(Domain::SelectionId project_id, Domain::SelectionId config_container_id) const
{
    const auto& project = m_workbench.project(project_id);
    const auto cc = project.find_config_container(config_container_id);
    const auto& selected_preset = cc->selected_preset();

    return system_preset_bed_shape(project_id, selected_preset);
}

Vec2ds PresetInteractor::system_preset_bed_shape(
    Domain::SelectionId project_id,
    const Domain::Preset::SelectedPreset& selected_preset
) const
{
    PrinterPresetProjectView view =
        get_printer_presets_view(project_id, selected_preset.hw_config.id);
    for (const auto [printer_preset_ref, is_runtime] : view.items()) {
        if (!is_runtime) {
            auto shape_item = printer_preset_ref.get().config_box().find("bed_shape");
            return (shape_item.item != nullptr) ? shape_item.item->value().get<Vec2ds>() : Vec2ds();
        }
    }

    const auto& hw_printer_preset =
        get_printer_preset(selected_preset.hw_config.id, selected_preset.printer.id).first.get();
    auto shape_item = hw_printer_preset.config_box().find("bed_shape");
    return (shape_item.item != nullptr) ? shape_item.item->value().get<Vec2ds>() : Vec2ds();
}

PresetInteractorConfigContainerContext&
PresetInteractor::mutable_selected_config_container_context()
{
    auto& project_ctx = get_project_context(m_selected_project_id)->second;
    auto& cccs        = project_ctx.config_containers;
    return cccs.find(project_ctx.selected_config_container_id)->second;
}

Domain::Preset::SelectedPreset& PresetInteractor::mutable_selected_printer_preset()
{
    auto& project   = m_workbench.project(m_selected_project_id);
    const auto& ccc = selected_config_container_context();
    auto* cc        = project.find_config_container(ccc.config_container_id);
    return cc->mutable_selected_preset();
}

namespace {

void update_hw_config_tools_and_materials_features_from_preset(
    Domain::Preset::SelectedPreset& preset
)
{
    using Domain::overloaded;
    using Domain::Preset::override_features;

    for (size_t i = 0, n = preset.materials.size(); i < n; i++) {
        const auto& src_mat = preset.materials[i];
        const auto address =
            Domain::Preset::MaterialIterator::from_slot_index(preset.hw_config, i).address();
        auto& dest_mat =
            preset.hw_config.materials[address];
        override_features(dest_mat.features, src_mat.features);
        // TODO: maybe we should use '$.type.abbre'
        std::visit(
            overloaded{
                [&dest_mat](const Domain::FilamentSettings& v)
                {
                    auto it = v.find("filament_type");
                    if (it.item)
                        dest_mat.type = it.item->get<std::string>();
                },
                [](const auto& v) {}
            },
            src_mat.values
        );
        dest_mat.id = src_mat.id;
    }
}

template <typename R>
auto get_first_from_range(R&& range)
{
    for (auto it : range)
        return it;
    PANIC("Empty range");
}

template <typename R>
auto get_named_or_first_from_range(R&& range, const std::string& name)
{
    if (!name.empty()) {
        for (auto it : range) {
            if (it.first.get().short_name() == name) {
                return it;
            }
        }
    }
    return get_first_from_range(range);
}

template <typename T>
void resize_tool_dependant_presets(
    const Domain::Preset::HwPrinterConfig& hw_config,
    std::vector<T>& presets
)
{
    constexpr bool is_tool = std::is_same_v<T, Domain::Preset::EvaluatedToolPrintPreset::Preset>;

    const bool is_sla = hw_config.technology == Domain::PrinterTechnology::SLA;
    // tool print presets for SLA are always empty
    const size_t slot_count =
        is_tool ? (is_sla ? 0 : hw_config.tool_count) : hw_config.material_slot_count();

    if (presets.size() > slot_count)
        presets.resize(slot_count);
    while (presets.size() < slot_count)
        presets.push_back(presets.empty() ? T{} : presets.back());
}

} // namespace

void PresetInteractor::fill_config_container_with_selected_preset(
    Domain::ConfigContainer& cc,
    const std::string& printer_hw_config_id,
    const std::string& printer_preset_id,
    bool printer_only,
    ListenerInvokeLaterBag& bag
)
{
    const auto& hw_config      = get_printer_config(printer_hw_config_id).first.get();

    if (printer_only) {
        auto& selected_preset     = cc.mutable_selected_preset();

        const Domain::Preset::PresetKind kind = Domain::Preset::printer_kind(selected_preset.technology());
        // process unsaved changes, if any exist before update selected preset
        process_all_save_changes(selected_preset, bag);

        const auto& printer_preset =
            get_printer_preset(printer_hw_config_id, printer_preset_id).first.get();
        selected_preset.hw_config = hw_config;
        selected_preset.printer   = printer_preset;

        resize_tool_dependant_presets(hw_config, selected_preset.tools);
        resize_tool_dependant_presets(hw_config, selected_preset.materials);

        // Set transferred values after switch to new selected preset
        process_operation_from_unsaved_changes(selected_preset, PresetDiffOperation::Transfer, bag, kind);

        return;
    }

    const auto& p = get_or_create_project_context(m_selected_project_id);
    const auto& preset_bundle = m_workbench.preset_bundle();
    const auto& printer = get_printer_preset(printer_hw_config_id, printer_preset_id).first.get();

    PrintPresetProjectView
        print_view(preset_bundle, p.runtime_presets, printer_hw_config_id, printer_preset_id);
    auto print = get_named_or_first_from_range(print_view.items(), get_string(printer, "default_print")).first;

    std::vector<Domain::Preset::EvaluatedToolPrintPreset::Preset> tools;

    if (hw_config.technology == Domain::PrinterTechnology::FFF) {
        for (size_t n = hw_config.tool_count, tool_idx = 0; tool_idx < n; ++tool_idx) {
            // TODO: better choose tool-print preset + ask for config values transfer
            ToolPrintPresetProjectView tool_view(
                preset_bundle,
                p.runtime_presets,
                printer_hw_config_id,
                printer_preset_id,
                print.get().id,
                tool_idx
            );
            tools.emplace_back(get_named_or_first_from_range(tool_view.items(), get_string(print.get(), "default_tool_print")).first.get());
        }
    }

    std::vector<Domain::Preset::EvaluatedMaterialPreset::Preset> materials;
    for (size_t n = hw_config.material_slot_count(), slot_idx = 0; slot_idx < n; ++slot_idx) {
        // TODO: better choose tool-print preset + ask for config values transfer
        MaterialPresetProjectView material_view(
            preset_bundle,
            p.runtime_presets,
            printer_hw_config_id,
            printer_preset_id,
            print.get().id,
            slot_idx
        );
        materials.emplace_back(
            get_named_or_first_from_range(
               material_view.items(),
               get_string(print.get(), "default_material")
            ).first.get()
        );
    }

    auto& selected_preset = cc.mutable_selected_preset();

    // save print_id as the `print` may get invalid after save
    std::string print_id = print.get().id;

    // process unsaved changes, if any exist before update selected preset
    process_all_save_changes(selected_preset, bag);
    const auto& reloaded_printer_preset =
        get_printer_preset(printer_hw_config_id, printer_preset_id).first.get();
    // we need to get fresh print preset reference, as the eventual save may invalidate `print`
    const auto& reloaded_print =
        get_print_preset(printer_hw_config_id, printer_preset_id, print_id).first.get();
    selected_preset = Domain::Preset::SelectedPreset{
        .hw_config = hw_config,
        .printer   = reloaded_printer_preset,
        .print     = reloaded_print,
        .tools     = std::move(tools),
        .materials = std::move(materials)
};

    // Set transfered values after switch to new selected preset
    process_operation_from_unsaved_changes(selected_preset, PresetDiffOperation::Transfer, bag);

    update_hw_config_tools_and_materials_features_from_preset(selected_preset);
}

namespace {

std::optional<size_t>
find_preferred(const std::vector<PresetItem>& items, const std::string& preferred)
{
    if (!preferred.empty() && !items.empty()) {
        auto it = std::ranges::find_if(
            items,
            [&preferred](const PresetItem& item) { return item.id == preferred; }
        );
        if (it != items.end()) {
            auto ret = std::distance(items.begin(), it);
            return ret;
        }

        it = std::ranges::find_if(
            items,
            [&preferred](const PresetItem& item) { return item.name == preferred; }
        );
        if (it != items.end()) {
            auto ret = std::distance(items.begin(), it);
            return ret;
        }
    }

    return std::nullopt;
}

void
set_items(PresetItemObservableList& dest, std::vector<PresetItem>&& items, size_t selected_index)
{
    ASSERT(selected_index == Domain::INVALID_ID || !items.empty());
    dest.items().set_items(std::move(items));
    dest.set_selected_index(selected_index);
}

template <typename V, typename TF, typename TS>
std::optional<size_t> set_items(
    PresetItemObservableList& dest,
    const V& source,
    const std::string& hw_config_id,
    const std::string& hw_config_name,
    bool hw_config_runtime,
    const Domain::Preset::EvaluatedPreset<TF, TS>& selected,
    const std::optional<std::string>& preferred
)
{
    std::optional<std::size_t> selected_index;
    std::optional<std::size_t> selected_by_name_index;
    std::vector<PresetItem> items;
    size_t idx = 0;

    for (const auto& [item_rw, is_runtime] : source.items()) {
        const auto& item = item_rw.get();
        std::string name;
        name = item.short_name();
        emplace_item(
            items,
            item.id,
            name,
            item.origin,
            hw_config_id,
            hw_config_name,
            hw_config_runtime
        );
        if (item.id == selected.id)
            selected_index = idx;
        if (item.short_name() == selected.short_name() && item.origin == selected.origin)
            selected_by_name_index = idx;
        idx++;
    }

    size_t selected_index_val = items.empty() ? Domain::INVALID_ID : 0;
    const bool use_preferred  = preferred.has_value()
        && (
            // is a new project,
            selected.id.empty()
            // or we don't find preset with same name as the old one
            || (!selected_index.has_value() && !selected_by_name_index.has_value())
            );
    if (use_preferred) {
        selected_index_val = find_preferred(items, preferred.value()).value_or(selected_index_val);
    } else {
        selected_index_val =
            selected_index.value_or(selected_by_name_index.value_or(selected_index_val));
    }
    set_items(dest, std::move(items), selected_index_val);
    return selected_index.has_value() ? std::nullopt : std::optional{selected_index_val};
}

} // namespace

void PresetInteractor::fill_printer_presets(bool no_data_update, ListenerInvokeLaterBag& bag)
{
    const auto& preset_bundle = m_workbench.preset_bundle();
    const auto& p             = get_or_create_project_context(m_selected_project_id);

    std::vector<PresetItem> items;
    const HwPrinterConfigProjectView config_view(preset_bundle, p.runtime_presets);
    for (const auto& [hw_config_rw, hw_config_runtime] : config_view.items()) {
        const auto& hw_config = hw_config_rw.get();
        const PrinterPresetProjectView printer_view(preset_bundle, p.runtime_presets, hw_config.id);
        for (const auto& [printer_rw, is_runtime] : printer_view.items()) {
            const auto& printer = printer_rw.get();
            emplace_item(
                items,
                printer.id,
                printer.name,
                printer.origin,
                hw_config.id,
                m_use_hw_config_short_name ? hw_config.short_name : hw_config.name,
                hw_config_runtime
            );
        }
    }

    std::ranges::sort(
        items,
        {},
        sort_key
    );

    std::optional<size_t> selected_index;
    std::optional<size_t> selected_index_by_hw;
    auto& selected_preset = mutable_selected_printer_preset();
    size_t idx = 0;
    for (const auto& item : items) {
        if (item.hw_printer_config_id == selected_preset.hw_config.id)
        {
            if (item.id == selected_preset.printer.id)
                selected_index = idx;
            else
                selected_index_by_hw = idx;
        }

        idx++;
    }
    m_printer_presets.items().set_items(std::move(items));
    size_t selected_index_val = selected_index.value_or(selected_index_by_hw.value_or(0));

    // make sure the new selection is propagated
    if (!selected_index.has_value()) {
        const auto& selected_item = m_printer_presets.items().at(selected_index_val);
        select_printer_preset_internal(
            selected_item.hw_printer_config_id,
            selected_item.id,
            no_data_update,
            bag
        );
    } else {
        const auto& [original_printer_preset_ref, printer_is_runtime] =
            get_printer_preset(selected_preset.hw_config.id, selected_preset.printer.id);
        m_printer_cbi_accessor.set_config_box(
            &selected_preset.printer.config_box(),
            &original_printer_preset_ref.get().config_box()
        );
    }

    // call set_selected_index after set all configs for m_printer_cbi_accessor
    m_printer_presets.set_selected_index(selected_index_val);
}

bool PresetInteractor::print_has_unsaved_changes() const
{
    return m_unsaved_changes.contains(PresetSwitchKindId{Domain::Preset::PresetKind::FdmPrint})
        || m_unsaved_changes.contains(PresetSwitchKindId{Domain::Preset::PresetKind::SlaPrint});
}

bool PresetInteractor::tool_print_has_unsaved_changes(size_t tool_index) const
{
    return m_unsaved_changes.contains(
        PresetSwitchKindId{Domain::Preset::PresetKind::FdmToolPrint, tool_index}
    );
}

bool PresetInteractor::material_has_unsaved_changes(size_t slot_index) const
{
    return m_unsaved_changes.contains(
               PresetSwitchKindId{Domain::Preset::PresetKind::FdmMaterial, slot_index}
           )
        || m_unsaved_changes.contains(
            PresetSwitchKindId{Domain::Preset::PresetKind::SlaMaterial, slot_index}
        );
}

void PresetInteractor::fill_print_presets(
    const Domain::Preset::SelectedPreset& selected_preset,
    bool no_data_update,
    ListenerInvokeLaterBag& bag
)
{
    const auto& p            = get_or_fail_project_context(m_selected_project_id);
    const auto& hw_config    = selected_preset.hw_config;
    const auto& hw_config_id = hw_config.id;
    bool hw_config_runtime   = !m_workbench.preset_bundle().printer_configs.contains(hw_config_id);
    const auto changed_selection_index = set_items(
        m_print_presets,
        PrintPresetProjectView(
            m_workbench.preset_bundle(),
            p.runtime_presets,
            hw_config_id,
            selected_preset.printer.id
        ),
        hw_config_id,
        m_use_hw_config_short_name ? hw_config.short_name : hw_config.name,
        hw_config_runtime,
        selected_preset.print,
        get_string(selected_preset.printer, "default_print")
    );
    const auto& item = m_print_presets.items().at(
        changed_selection_index.value_or(m_print_presets.selected_index())
    );
    select_print_preset_internal(item.id, no_data_update, bag);
}

void PresetInteractor::fill_tools_presets(
    const Domain::Preset::SelectedPreset& selected_preset,
    bool no_data_update,
    ListenerInvokeLaterBag& bag
)
{
    const Domain::Preset::HwPrinterConfig& hw_config = selected_preset.hw_config;
    bool hw_config_runtime = !m_workbench.preset_bundle().printer_configs.contains(hw_config.id);
    std::vector<PresetItemObservableList> tools;

    const size_t tool_count =
        hw_config.technology == Domain::PrinterTechnology::SLA ? 0 : hw_config.tool_count;
    ASSERT(
        hw_config.technology != Domain::PrinterTechnology::FFF || tool_count > 0,
        selected_preset.print.id
    );
    const bool has_tool_prints = hw_config.technology == Domain::PrinterTechnology::FFF;
    std::vector<std::optional<size_t>> changed_selected_indices;

    if (has_tool_prints) {
        ASSERT(tool_count == selected_preset.tools.size());

        const auto& p = get_or_fail_project_context(m_selected_project_id);
        for (size_t n = selected_preset.hw_config.tool_count, tool_index = 0; tool_index < n;
             ++tool_index)
        {
            PresetItemObservableList items;
            ToolPrintPresetProjectView view(
                m_workbench.preset_bundle(),
                p.runtime_presets,
                hw_config.id,
                selected_preset.printer.id,
                selected_preset.print.id,
                tool_index
            );

            auto changes_selected_index = set_items(
                items,
                view,
                hw_config.id,
                m_use_hw_config_short_name ? hw_config.short_name : hw_config.name,
                hw_config_runtime,
                selected_preset.tools[tool_index],
                get_string(selected_preset.print, "default_tool_print")
            );

            tools.emplace_back(std::move(items));
            changed_selected_indices.emplace_back(changes_selected_index);
        }
    }

    m_tool_print_presets.set_items(std::move(tools));
    if (has_tool_prints) {
        for (size_t n = selected_preset.hw_config.tool_count, tool_index = 0; tool_index < n;
             ++tool_index)
        {
            const auto changed_selected_index = changed_selected_indices.at(tool_index);
            const auto& tool_print_presets = m_tool_print_presets.at(tool_index);
            const auto& item = tool_print_presets.items().at(
                changed_selected_index.value_or(tool_print_presets.selected_index())
            );
            select_tool_print_preset_internal(tool_index, item.id, no_data_update, bag);
        }
    }

}

void PresetInteractor::fill_materials_presets(
    Domain::Preset::SelectedPreset& selected_preset,
    bool no_data_update,
    ListenerInvokeLaterBag& bag
)
{
    fill_selected_material_cbis(selected_preset);

    std::vector<PresetItemObservableList> materials;
    std::vector<std::optional<size_t>> changed_selected_indices;
    const auto& p          = get_or_fail_project_context(m_selected_project_id);
    const auto& hw_config  = selected_preset.hw_config;
    bool hw_config_runtime = !m_workbench.preset_bundle().printer_configs.contains(hw_config.id);
    for (size_t n = hw_config.material_slot_count(), slot_index = 0; slot_index < n; ++slot_index) {
        PresetItemObservableList items;
        MaterialPresetProjectView view(
            m_workbench.preset_bundle(),
            p.runtime_presets,
            hw_config.id,
            selected_preset.printer.id,
            selected_preset.print.id,
            slot_index
        );
        auto changed_selected_index = set_items(
            items,
            view,
            hw_config.id,
            m_use_hw_config_short_name ? hw_config.short_name : hw_config.name,
            hw_config_runtime,
            selected_preset.materials[slot_index],
            get_string(selected_preset.print, "default_material")
        );
        changed_selected_indices.emplace_back(changed_selected_index);
        materials.emplace_back(std::move(items));
    }
    m_material_presets.set_items(std::move(materials));

    for (size_t n = hw_config.material_slot_count(), slot_index = 0; slot_index < n; ++slot_index) {
        const auto changed_selected_index = changed_selected_indices.at(slot_index);
        const auto& material_presets = m_material_presets.at(slot_index);
        const auto& item             = material_presets.items().at(
            changed_selected_index.value_or(material_presets.selected_index())
        );
        select_material_preset_internal(slot_index, item.id, no_data_update, bag);
    }
}

PresetInteractor::ToolItems PresetInteractor::get_tool_items(
    const Domain::Preset::HwPrinterConfig& hw_config)
{
    ToolItems result;

    HwConfigEvaluator config_eval;
#if DEBUG_CONDITION_EVAL
    config_eval.set_debug_output([](std::string_view message)
    {
        SPDLOG_INFO(message);
    });
#endif // DEBUG_CONDITION_EVAL
    const auto& tools = hw_config.vendor_id.empty() ? std::map<std::string, Domain::Preset::HwToolConfigDef>{} : (m_workbench.preset_bundle()
            .vendor_bundles.at(hw_config.vendor_id)
            .vendor_data.defs.at(hw_config.technology)
            .tools);
    HwToolConfigIterator iterator = config_eval.iterate_tools(
        hw_config,
        tools

    );

    for (; iterator != std::end(iterator); ++iterator) {
        result.tool_defs.push_back(*iterator);
    }

    for (size_t tool_index = 0; tool_index < hw_config.tool_count; ++tool_index) {
        // Find selected Tool
        const std::string selected_id = hw_config.tools.at(tool_index).id;
        size_t selected_index         = 0;
        for (HwToolConfigIterator selected_tool_iterator = iterator.begin();
             selected_tool_iterator != iterator.end();
             ++selected_tool_iterator)
        {
            if (selected_id == selected_tool_iterator->id) {
                selected_index = std::distance(iterator.begin(), selected_tool_iterator);
                break;
            }
        }

        result.selected_tools.push_back(selected_index);
    }

    return result;
}

void PresetInteractor::fill_tool_items(const Domain::Preset::HwPrinterConfig& hw_config)
{
    const ToolItems tool_items{get_tool_items(hw_config)};

    const std::vector<Domain::Preset::HwToolConfigDef>& items{tool_items.tool_defs};

    std::vector<ToolConfigItemObservableList> result;
    result.reserve(tool_items.selected_tools.size());
    for (std::size_t i{}; i < tool_items.selected_tools.size(); ++i) {
        const std::size_t& selected_index{tool_items.selected_tools[i]};

        ToolConfigItemObservableList& item = result.emplace_back();
        item.items().set_items(items);
        item.set_selected_index(selected_index);
    }
    m_tool_items.set_items(std::move(result));
}

PresetInteractor::SheetItems PresetInteractor::get_sheet_items(
    const Domain::Preset::HwPrinterConfig& hw_config)
{
    SheetItems result;

    HwConfigEvaluator config_eval;
    HwSheetConfigIterator iterator = config_eval.iterate_sheets(
        hw_config,
        m_workbench.preset_bundle()
            .vendor_bundles.at(hw_config.vendor_id)
            .vendor_data.defs.at(hw_config.technology)
            .sheets
    );

    for (; iterator != std::end(iterator); ++iterator) {
        result.sheet_defs.push_back(*iterator);
        if (iterator->id == hw_config.sheet.id) {
            result.selected_sheet = std::distance(std::begin(iterator), iterator);
        }
    }

    return result;
}

void PresetInteractor::fill_sheet_items(const Domain::Preset::HwPrinterConfig& hw_config)
{
    const auto [items, selected_index]{get_sheet_items(hw_config)};
    m_sheet_items.items().set_items(items);
    if (!items.empty()) { // SLA doesn't yet have sheets specified
        m_sheet_items.set_selected_index(selected_index);
    }
}

void PresetInteractor::fill_selected_material_cbis(
    Domain::Preset::SelectedPreset& selected_preset,
    bool force_fill_originals
)
{
    // Remove previous material accessors
    m_material_accessors.clear();

    size_t material_index{0};

    std::vector<OverridableConfigBoxInteractor::ConfigBoxes> material_cbs;
    material_cbs.reserve(selected_preset.materials.size());
    std::transform(
        selected_preset.materials.begin(),
        selected_preset.materials.end(),
        std::back_inserter(material_cbs),
        [&](Domain::Preset::EvaluatedMaterialPreset::Preset& preset)
            -> OverridableConfigBoxInteractor::ConfigBoxes
        {
            const Domain::ConfigBox* original_config_box{nullptr};
            if (force_fill_originals) {
                const auto& [original_material_preset_ref, b] = get_material_preset(
                    selected_preset.hw_config.id,
                    selected_preset.printer.id,
                    selected_preset.print.id,
                    material_index,
                    preset.id
                );
                material_index++;
                original_config_box = &original_material_preset_ref.get().config_box();
            }
            return {&preset.config_box(), original_config_box};
        }
    );

    SetAccessorMap accessors = m_material_cbi_list.set_items(material_cbs);
    m_material_accessors.insert(accessors.begin(), accessors.end());
}

void PresetInteractor::select_printer_preset_internal(
    const std::string& printer_hw_config_id,
    const std::string printer_preset_id, // intentionally copied
    bool no_data_update,
    ListenerInvokeLaterBag& bag
)
{
    auto& project   = m_workbench.project(m_selected_project_id);
    const auto& ccc = selected_config_container_context();
    auto* cc        = project.find_config_container(ccc.config_container_id);
    ASSERT(cc != nullptr, ccc.config_container_id);
    get_or_fail_project_context(m_selected_project_id).invalid_hw_config = std::nullopt;

    if (!no_data_update) {
        fill_config_container_with_selected_preset(*cc, printer_hw_config_id, printer_preset_id, true, bag);
    }

    Domain::Preset::SelectedPreset& selected_preset = mutable_selected_printer_preset();

    fill_tool_items(selected_preset.hw_config);
    fill_sheet_items(selected_preset.hw_config);

    const auto& [original_printer_preset_ref, printer_is_runtime] =
        get_printer_preset(selected_preset.hw_config.id, selected_preset.printer.id);
    m_printer_cbi_accessor.set_config_box(
        &selected_preset.printer.config_box(),
        &original_printer_preset_ref.get().config_box()
    );

    m_printer_presets.set_selected(
        [printer_hw_config_id, printer_preset_id](const PresetItem& item)
        {
            return item.id == printer_preset_id
                && item.hw_printer_config_id == printer_hw_config_id;
        }
    );

    fill_print_presets(selected_preset, no_data_update, bag);
    fill_tools_presets(selected_preset, no_data_update, bag);
    fill_materials_presets(selected_preset, no_data_update, bag);

    update_print_tool_cbi(selected_preset, ccc.config_container_id);

    bag.add([this, project_id = m_selected_project_id, cc]{
        // notify on change
        invoke_listeners<IPresetChangedListener>(
            [&cc, project_id](auto* l)
            { l->on_preset_selection_changed(project_id, cc->id().id, PresetItemType::PrinterPreset); }
        );
    });
}

void PresetInteractor::select_printer_preset(
    const std::string& printer_hw_config_id,
    const std::string& printer_preset_id,
    bool user
)
{
    ListenerInvokeLaterBag bag;
    select_printer_preset_internal(printer_hw_config_id, printer_preset_id, false, bag);
    bag.add([this] { invoke_slicing_input_changed(); });

    if (user) {
        m_backup_store.invalidate_backup(m_selected_project_id);
    }
}

void
PresetInteractor::select_print_preset_internal(
    const std::string id,  // intentionally copied
    bool no_data_update,
    ListenerInvokeLaterBag& bag
)
{
    Domain::Preset::SelectedPreset& selected_preset = mutable_selected_printer_preset();
    const auto& hw_config                           = selected_preset.hw_config;

    const Domain::Preset::PresetKind kind = Domain::Preset::print_kind(selected_preset.technology());
    process_all_save_changes(selected_preset, bag);

    // Fetch the presets after (!) the save operation perform
    // (which may modify the stored presets in vector, which will turn the `p` invalid).
    const auto& printer = get_printer_preset(hw_config.id, selected_preset.printer.id).first.get();
    const auto& p = get_print_preset(hw_config.id, printer.id, id).first.get();

    if (!no_data_update) {
        selected_preset.print = p;
        process_operation_from_unsaved_changes(selected_preset, PresetDiffOperation::Transfer, bag, kind);
    }

    m_print_presets.set_selected([&id](const PresetItem& item) { return item.id == id; });

    fill_tools_presets(selected_preset, no_data_update, bag);
    fill_materials_presets(selected_preset, no_data_update, bag);

    const auto& ccc = selected_config_container_context();
    const Domain::SelectionId config_container_id{ccc.config_container_id};
    update_print_tool_cbi(selected_preset, config_container_id);

    bag.add([this, project_id = m_selected_project_id, config_container_id]{
        invoke_listeners<IPresetChangedListener>(
            [project_id, config_container_id](auto* l)
            {
                l->on_preset_selection_changed(
                    project_id,
                    config_container_id,
                    PresetItemType::PrintPreset
                );
            }
        );
    });
}

void PresetInteractor::select_print_preset(const std::string& id, bool user)
{
    ListenerInvokeLaterBag bag;
    select_print_preset_internal(id, false, bag);
    bag.add([this]{ invoke_slicing_input_changed(); });
    if (user) {
        m_backup_store.invalidate_backup(m_selected_project_id);
    }
}

void PresetInteractor::select_tool_print_preset_internal(
    size_t tool_index,
    const std::string id,  // intentionally copied
    bool no_data_update,
    ListenerInvokeLaterBag& bag
)
{
    auto& selected_preset = mutable_selected_printer_preset();

    const Domain::Preset::PresetKind kind = Domain::Preset::tool_print_kind(selected_preset.technology());
    if (!no_data_update) {
        process_all_save_changes(selected_preset, bag);
        const auto& t         = get_tool_print_preset(
            selected_preset.hw_config.id,
            selected_preset.printer.id,
            selected_preset.print.id,
            tool_index,
            id
        ).first.get();
        selected_preset.tools[tool_index] = t;
        process_operation_from_unsaved_changes(selected_preset, PresetDiffOperation::Transfer, bag, kind, tool_index);
    }

    m_tool_print_presets_writer.mutate_at(
        tool_index,
        [&id](auto& item)
        { item.set_selected([&id](const PresetItem& item) { return item.id == id; }); }
    );

    const auto& ccc = selected_config_container_context();
    const Domain::SelectionId config_container_id{ccc.config_container_id};

    bag.add([this, project_id = m_selected_project_id, config_container_id]{
        auto& selected_preset = mutable_selected_printer_preset();
        update_print_tool_cbi(selected_preset, config_container_id);
        invoke_listeners<IPresetChangedListener>(
            [project_id, config_container_id](auto* l)
            {
                l->on_preset_selection_changed(
                    project_id,
                    config_container_id,
                    PresetItemType::ToolPrintPreset
                );
            }
        );
    });
}

void PresetInteractor::select_tool_print_preset(size_t tool_index, const std::string& id, bool user)
{
    ListenerInvokeLaterBag bag;
    select_tool_print_preset_internal(tool_index, id, false, bag);
    bag.add([this] { invoke_slicing_input_changed(); });

    if (user) {
        m_backup_store.invalidate_backup(m_selected_project_id);
    }
}

void PresetInteractor::select_material_preset_internal(
    size_t material_index,
    const std::string id, // intentionally copied
    bool no_data_update,
    ListenerInvokeLaterBag& bag
)
{
    auto& selected_preset = mutable_selected_printer_preset();

    const Domain::Preset::PresetKind kind = Domain::Preset::material_kind(selected_preset.technology());
    if (!no_data_update) {
        process_all_save_changes(selected_preset, bag);
        const auto& m         = get_material_preset(
            selected_preset.hw_config.id,
            selected_preset.printer.id,
            selected_preset.print.id,
            material_index,
            id
        ).first.get();
        selected_preset.materials[material_index] = m;
        process_operation_from_unsaved_changes(selected_preset, PresetDiffOperation::Transfer, bag, kind, material_index);
    }

    const auto& [original_material_preset_ref, is_runtime] = get_material_preset(
        selected_preset.hw_config.id,
        selected_preset.printer.id,
        selected_preset.print.id,
        material_index,
        selected_preset.materials[material_index].id
    );

    m_material_accessors.at(&m_material_cbi_list.at(material_index))
        .set_config_box(
            &selected_preset.materials.at(material_index).config_box(),
            &original_material_preset_ref.get().config_box()
        );

    update_hw_config_tools_and_materials_features_from_preset(selected_preset);

    m_material_presets_writer.mutate_at(
        material_index,
        [&id](auto& item)
        { item.set_selected([&id](const PresetItem& item) { return item.id == id; }); }
    );

    const auto& ccc = selected_config_container_context();
    const Domain::SelectionId config_container_id{ccc.config_container_id};
    bag.add([this, project_id = m_selected_project_id, config_container_id]{
        invoke_listeners<IPresetChangedListener>(
            [project_id, config_container_id](auto* l)
            {
                l->on_preset_selection_changed(
                    project_id,
                    config_container_id,
                    PresetItemType::MaterialPreset
                );
            }
        );
    });
}

void PresetInteractor::select_material_preset(size_t material_index, const std::string& id, bool user)
{
    ListenerInvokeLaterBag bag;
    select_material_preset_internal(material_index, id, false, bag);
    bag.add([this] { invoke_slicing_input_changed(); });

    if (user) {
        m_backup_store.invalidate_backup(m_selected_project_id);
    }
}

void PresetInteractor::duplicate_hw_config_if_needed_and_update(
    Domain::Preset::HwPrinterConfig& hw_config,
    ListenerInvokeLaterBag& bag
)
{
    auto& p = get_or_create_project_context(m_selected_project_id);
    bool is_runtime = p.runtime_presets.printer_configs.contains(hw_config.id);
    const bool dup_needed =
        is_runtime
        || std::ranges::count_if(
            m_workbench.project(p.project_id).config_containers(),
            [&](const auto& cc)
            { return cc->selected_preset().hw_config.id == hw_config.id; }
        ) > 1;
    if (dup_needed) {
        auto new_config = hw_config;
        new_config.id   = generate_uuid();
        hw_config       = new_config;
    }
    // update hw config
    if (is_runtime) {
        p.runtime_presets.printer_configs[hw_config.id] = hw_config;
    } else {
        auto& preset_bundle = m_workbench.preset_bundle();
        preset_bundle.printer_configs[hw_config.id] = hw_config;
        if (dup_needed)
            preset_bundle.vendor_bundles[hw_config.vendor_id].printer_configs.push_back(hw_config);
    }
    fill_printer_presets(false, bag);
}

bool PresetInteractor::select_printer_tool_item(size_t tool_index, const std::string& id, bool user)
{
    auto& selected_preset = mutable_selected_printer_preset();
    auto& p = get_or_fail_project_context(m_selected_project_id);

    // we continue with p.invalid_hw_config if it is set (e.g. from previous call)
    // and it matches the id of selected config container (i.e. no printer was changed)
    const bool use_invalid_hw_config =
        p.invalid_hw_config.has_value() && selected_preset.hw_config.id == p.invalid_hw_config->id;
    auto hw_config =
        use_invalid_hw_config ? p.invalid_hw_config.value() : selected_preset.hw_config;
    const auto& vendor_data =
        m_workbench.preset_bundle().vendor_bundles.at(hw_config.vendor_id).vendor_data;
    const auto* tool_def = vendor_data.find_tool_config_def_by_id(id);
    ASSERT(tool_def != nullptr, id);
    hw_config.tools.at(tool_index) = from_def(vendor_data, *tool_def);
    hw_config.name                 = Domain::Preset::suggest_name(hw_config, vendor_data, false);
    hw_config.short_name           = Domain::Preset::suggest_name(hw_config, vendor_data, true);

    const bool successfully_changed = update_changed_selected_preset_hw_config(hw_config);
    if (!successfully_changed) {
        p.invalid_hw_config = hw_config;
    } else {
        selected_preset.hw_config = hw_config;
        p.invalid_hw_config = std::nullopt;

        const auto& ccc = selected_config_container_context();
        const Domain::SelectionId config_container_id{ccc.config_container_id};
        invoke_listeners<IPresetChangedListener>(
            [project_id = m_selected_project_id, config_container_id](auto* l)
            {
                l->on_hw_item_selection_changed(
                    project_id,
                    config_container_id,
                    HwItemType::ToolItem
                );
            }
        );

        if (user) {
            m_backup_store.invalidate_backup(m_selected_project_id);
        }

        invoke_slicing_input_changed();
    }

    return successfully_changed;
}

bool PresetInteractor::select_printer_sheet(const std::string& id, bool user)
{
    auto& p = get_or_fail_project_context(m_selected_project_id);
    auto& selected_preset = mutable_selected_printer_preset();

    const auto& vendor_data = m_workbench.preset_bundle()
                                  .vendor_bundles.at(selected_preset.hw_config.vendor_id)
                                  .vendor_data;
    const auto* sheet_def = vendor_data.find_sheet_config_def_by_id(id);
    ASSERT(sheet_def != nullptr, id);

    // we continue with p.invalid_hw_config if it is set (e.g. from previous call)
    // and it matches the id of selected config container (i.e. no printer was changed)
    const bool use_invalid_hw_config =
        p.invalid_hw_config.has_value() && selected_preset.hw_config.id == p.invalid_hw_config->id;
    auto hw_config =
        use_invalid_hw_config ? p.invalid_hw_config.value() : selected_preset.hw_config;

    hw_config.sheet = from_def(vendor_data, *sheet_def);
    const bool successfully_changed = update_changed_selected_preset_hw_config(hw_config);
    if (!successfully_changed) {
        p.invalid_hw_config = hw_config;
    } else {
        selected_preset.hw_config = hw_config;
        p.invalid_hw_config = std::nullopt;

        const auto& ccc = selected_config_container_context();
        const Domain::SelectionId config_container_id{ccc.config_container_id};
        invoke_listeners<IPresetChangedListener>(
            [project_id = m_selected_project_id, config_container_id](auto* l)
            {
                l->on_hw_item_selection_changed(
                    project_id,
                    config_container_id,
                    HwItemType::SheetItem
                );
            }
        );

        if (user) {
            m_backup_store.invalidate_backup(m_selected_project_id);
        }
    }

    return successfully_changed;
}

Domain::Preset::PresetNames PresetInteractor::get_all_vendor_preset_names(
    Domain::Preset::PresetKind kind,
    const std::optional<std::string>& vendor_id
) const
{
    const auto& vendor_bundles     = m_workbench.preset_bundle().vendor_bundles;
    auto vendor_it =
        vendor_bundles.find(vendor_id.value_or(selected_printer_preset().hw_config.vendor_id));
    ASSERT(vendor_it != vendor_bundles.end());
    const auto& vendor_bundle = vendor_it->second;
    auto ret = vendor_bundle.preset_names.at(kind);

    IO::Details::PresetNamesMap runtime_names;

    const auto& runtime_presets =
        get_project_context(m_selected_project_id)->second.runtime_presets;

    auto append_names = [&runtime_names]<typename T>(const T& values)
    {
        for (const auto& v : values) {
            auto it = runtime_names.find(v.name);
            if (it != runtime_names.end()) {
                it->second.id.insert(v.id);
                it->second.root_id.insert(v.root_id);
            } else {
                runtime_names.emplace(
                    v.name,
                    Domain::Preset::PresetName{
                        v.name,
                        {v.id},
                        {v.root_id},
                        Domain::Preset::PresetOrigin::Runtime
                    }
                );
            }
        }
    };

    switch (kind) {
    case Domain::Preset::PresetKind::FdmPrinter:
    case Domain::Preset::PresetKind::SlaPrinter:
        append_names(runtime_presets.printer | std::views::values | std::views::join);
        break;

        case Domain::Preset::PresetKind::FdmPrint:
    case Domain::Preset::PresetKind::SlaPrint:
        append_names(runtime_presets.print | std::views::values | std::views::join);
        break;

    case Domain::Preset::PresetKind::FdmMaterial:
    case Domain::Preset::PresetKind::SlaMaterial:
        append_names(runtime_presets.material | std::views::values | std::views::join | std::views::join);
        break;

    case Domain::Preset::PresetKind::FdmToolPrint:
    case Domain::Preset::PresetKind::SlaToolPrint:
        append_names(runtime_presets.tool_print | std::views::values | std::views::join | std::views::join);
    }

    std::ranges::copy(runtime_names | std::views::values, std::back_inserter(ret));

    return ret;
}

const Domain::Preset::FeatureDefs* PresetInteractor::get_vendor_tool_feature_defs(
    const std::optional<std::string>& vendor_id
) const
{
    const auto& vendor_bundles = m_workbench.preset_bundle().vendor_bundles;
    const auto vendor_it =
        vendor_bundles.find(vendor_id.value_or(selected_printer_preset().hw_config.vendor_id));

    if (vendor_it == vendor_bundles.end()) {
        return nullptr;
    }

    return &vendor_it->second.vendor_data.info.features.tool;
}

boost::filesystem::path PresetInteractor::selected_user_preset_path(
    Domain::Preset::PresetKind kind,
    const std::string& preset_name
) const
{
    const auto& selected_preset = selected_printer_preset();

    return IO::preset_path(
        m_bundle_paths,
        kind,
        preset_name,
        selected_preset.hw_config.vendor_id,
        selected_preset.hw_config.repo_id
    );
}

void PresetInteractor::set_preset_value(
    Domain::ConfigLocation location,
    int element_idx,
    const std::string& name,
    ConfigItemModifyFn modify_fn
)
{
    auto& selected_preset = mutable_selected_printer_preset();
    std::optional<Domain::ConfigBox*> config_box_opt;
    if (selected_preset.technology() == Domain::PrinterTechnology::FFF) {
        auto loc = std::get<Domain::FDMConfigLocation>(location);

        switch (loc) {
        case Domain::FDMConfigLocation::Printer:
            config_box_opt = &std::get<Domain::PrinterSettings>(selected_preset.printer.values);
            break;

        case Domain::FDMConfigLocation::Print:
            config_box_opt = &std::get<Domain::PrintSettings>(selected_preset.print.values);
            break;

        case Domain::FDMConfigLocation::Tool:
            config_box_opt =
                &std::get<Domain::ToolPrintSettings>(selected_preset.tools[element_idx].values);
            break;

        case Domain::FDMConfigLocation::Filament:
            config_box_opt =
                &std::get<Domain::FilamentSettings>(selected_preset.materials[element_idx].values);
            break;

        default:
            PANIC("Unsupported location {}", loc);
        }
    } else {
        auto loc = std::get<Domain::SLAConfigLocation>(location);
        switch (loc) {
        case Domain::SLAConfigLocation::Printer:
            config_box_opt = &std::get<Domain::SLAPrinterSettings>(selected_preset.printer.values);
            break;

        case Domain::SLAConfigLocation::Print:
            config_box_opt = &std::get<Domain::SLAPrintSettings>(selected_preset.print.values);
            break;

        case Domain::SLAConfigLocation::Material:
            config_box_opt = &std::get<Domain::SLAMaterialSettings>(
                selected_preset.materials[element_idx].values
            );
            break;

        default:
            PANIC("Unsupported location {}", loc);
        }
    }

    ASSERT(config_box_opt.has_value());
    auto& config_box = *config_box_opt.value();
    auto it          = config_box.find(name);
    ASSERT(it.item != nullptr);
    modify_fn(*it.item);

    invoke_on_preset_value_changed(*it.item);
}

ConfigBoxInteractor& PresetInteractor::printer_cbi()
{
    return m_printer_cbi;
}

OverridableCBIObservableList& PresetInteractor::material_cbi_list()
{
    return m_material_cbi_list;
}

const OverridableCBIObservableList& PresetInteractor::material_cbi_list() const
{
    return m_material_cbi_list;
}

PrintToolConfigBoxInteractor& PresetInteractor::print_tool_cbi()
{
    return m_print_tool_cbi;
}

namespace {

// A local helper to route location logic, preventing header pollution.
// It takes lambdas for each specific operation type.
template <
    typename LocationVariant,
    typename PrinterOp,
    typename PrintOp,
    typename MaterialOp,
    typename ToolOp,
    typename ObjectOp>
auto apply_location_ops(
    const LocationVariant& loc_variant,
    PrinterOp printer_op,
    PrintOp print_op,
    MaterialOp material_op,
    ToolOp tool_op,
    ObjectOp object_op
)
{
    // Deduce the return type based on the first operation (PrinterOp)
    using ReturnType = std::invoke_result_t<PrinterOp>;

    return std::visit(
        [&](auto&& location) -> ReturnType
        {
            using T = std::decay_t<decltype(location)>;

            if constexpr (std::is_same_v<T, Domain::FDMConfigLocation>) {
                switch (location) {
                case Domain::FDMConfigLocation::Printer:
                    return printer_op();
                case Domain::FDMConfigLocation::Print:
                    return print_op();
                case Domain::FDMConfigLocation::Filament:
                    return material_op();
                case Domain::FDMConfigLocation::Tool:
                    return tool_op();
                case Domain::FDMConfigLocation::Object:
                case Domain::FDMConfigLocation::Volume:
                    return object_op();
                default:
                    break;
                }
            } else if constexpr (std::is_same_v<T, Domain::SLAConfigLocation>) {
                switch (location) {
                case Domain::SLAConfigLocation::Printer:
                    return printer_op();
                case Domain::SLAConfigLocation::Print:
                    return print_op();
                case Domain::SLAConfigLocation::Material:
                    return material_op();
                case Domain::SLAConfigLocation::Object:
                    return object_op();
                default:
                    break;
                }
            }

            // Handle the default fall-through case based on the expected return type
            if constexpr (std::is_void_v<ReturnType>) {
                return;
            } else if constexpr (std::is_same_v<ReturnType, bool>) {
                return false;
            } else {
                return nullptr;
            }
        },
        loc_variant
    );
}

} // anonymous namespace

const Domain::ConfigValue*
PresetInteractor::get_override_original_value(const Domain::ConfigItem& item, size_t index) const
{
    const std::string& name = item.name();

    return apply_location_ops(item.def().location,
        [&]() { return m_printer_cbi.find(name); },
        [&]() { return m_print_tool_cbi.find_print_value(name); },
        [&]() { return m_material_cbi_list.at(index).find(name); },
        [&]() { return m_print_tool_cbi.find_tool_value(name, index); },
        [&]() { return m_object_settings_interactor_accessor.find_object_value(name, index); }
    );
}

void PresetInteractor::set_from_original_value(const Domain::ConfigItem& item, size_t index)
{
    const std::string& name = item.name();

    apply_location_ops(item.location(),
        [&]() { m_printer_cbi_accessor.set_from_original_value(name); },
        [&]() { m_print_tool_cbi_accessor.set_from_original_print_value(name); },
        [&]() { m_material_accessors.at(&m_material_cbi_list.at(index)).set_from_original_value(name); },
        [&]() { m_print_tool_cbi_accessor.set_from_original_tool_value(name, index);},
        [&]() { /* m_object_settings_interactor_accessor.set_from_original_value(name, value); // TODO */ }
    );

    m_backup_store.invalidate_backup(m_selected_project_id);
    invoke_on_preset_value_changed(item);
    invoke_slicing_input_changed();
}

void PresetInteractor::set_item_value(
    const Domain::ConfigItem& item,
    const Domain::ConfigValue& value,
    const std::vector<size_t>& indexes
)
{
    // This is a temporary dummy way how to set items, a whole dependency resolving with overrides needs
    // to be ported here from the Legacy code

    const std::string& name = item.name();
    apply_location_ops(
        item.location(),
        [&]() { m_printer_cbi_accessor.set_value(name, value); },
        [&]() { m_print_tool_cbi_accessor.set_print_value(name, value); },
        [&]()
        {
            for (size_t index : indexes) {
                m_material_accessors.at(&m_material_cbi_list.at(index)).set_value(name, value);
            }
        },
        [&]() { m_print_tool_cbi_accessor.set_tool_value(name, indexes, value); },
        [&]() { m_object_settings_interactor_accessor.set_value(name, value); }
    );
    m_backup_store.invalidate_backup(m_selected_project_id);
    invoke_on_preset_value_changed(item);
    invoke_slicing_input_changed();
}


void PresetInteractor::set_item_override(const Domain::ConfigItem& item, bool enable, size_t index)
{
    // This is a temporary dummy way how to set items, a whole dependency resolving with overrides needs
    // to be ported here from the Legacy code

    const std::string& name = item.name();
    apply_location_ops(
        item.location(),
        [&]()
        {
            PANIC(
                "Invalid state: attempting to set an override value for a printer without override support."
            );
        },
        [&]() {},
        [&]()
        { m_material_accessors.at(&m_material_cbi_list.at(index)).set_override(name, enable); },
        [&]() { m_print_tool_cbi_accessor.set_tool_override(name, index, enable); },
        [&]()
        {
            if (!enable) {
                // For Objects and Volumes, revert the overridden value to the original value
                // when removing the override.
                if (const auto* original_value = get_override_original_value(item, 0)) {
                    m_object_settings_interactor_accessor.set_value(item.name(), *original_value);
                }
            }
            m_object_settings_interactor_accessor.set_override(name, enable);
        }
    );

    m_backup_store.invalidate_backup(m_selected_project_id);
    invoke_on_preset_value_changed(item);
    invoke_slicing_input_changed();
}

void PresetInteractor::invoke_slicing_input_changed()
{
    const auto& ccc     = selected_config_container_context();
    const auto& project = m_workbench.project(m_selected_project_id);
    for (const auto& instance :
         project.find_config_container(ccc.config_container_id)->bed_instances())
    {
        invoke_listeners<ISlicingInputChangedListener>(
            [&](auto listener)
            { listener->on_slicing_input_changed({ccc.config_container_id, instance->id().id}); }
        );
    }
}

void PresetInteractor::invoke_on_preset_value_changed(const Domain::ConfigItem& config_item)
{
    const auto& ccc = selected_config_container_context();
    invoke_listeners<IPresetChangedListener>(
        [&](auto listener)
        {
            listener->on_preset_value_changed(
                m_selected_project_id,
                ccc.config_container_id,
                config_item
            );
        }
    );
}

PresetsSwitchStates::iterator PresetInteractor::find_unsaved_change(
    PresetDiffOperation operation,
    Domain::Preset::PresetKind kind,
    std::optional<size_t> tool_id
)
{
    return std::find_if(
        m_unsaved_changes.begin(),
        m_unsaved_changes.end(),
        [&](const auto& item)
        {
            return item.first == Biz::Preset::PresetSwitchKindId(kind, tool_id)
                && item.second.operation == operation;
        }
    );
}

void PresetInteractor::process_all_save_changes(
    Domain::Preset::SelectedPreset& selected_preset,
    InvokeLaterBag& bag
)
{

    const auto& hw_config = selected_preset.hw_config;
    auto technology = hw_config.technology;
    process_operation_from_unsaved_changes(
        selected_preset,
        PresetDiffOperation::Save,
        bag,
        Domain::Preset::printer_kind(technology)
    );
    process_operation_from_unsaved_changes(
        selected_preset,
        PresetDiffOperation::Save,
        bag,
        Domain::Preset::print_kind(technology)
    );
    if (technology == Domain::PrinterTechnology::FFF) {
        for (size_t i = 0, n = hw_config.tool_count; i < n; i++) {
            process_operation_from_unsaved_changes(
                selected_preset,
                PresetDiffOperation::Save,
                bag,
                Domain::Preset::tool_print_kind(hw_config.technology),
                i
            );

        }
    }
    for (size_t i = 0, n = hw_config.materials.size(); i < n; i++) {
        process_operation_from_unsaved_changes(
            selected_preset,
            PresetDiffOperation::Save,
            bag,
            Domain::Preset::material_kind(hw_config.technology),
            i
        );
    }
}

void PresetInteractor::process_operation_from_unsaved_changes(
    Domain::Preset::SelectedPreset& selected_preset,
    PresetDiffOperation operation,
    ListenerInvokeLaterBag& bag,
    std::optional<Domain::Preset::PresetKind> kind /* = std::nullopt*/,
    std::optional<size_t> tool_id /* = std::nullopt*/
)
{
    auto process = [this, &bag](
                       Domain::Preset::SelectedPreset& selected_preset,
                       const Biz::Preset::PresetSwitchKindId& preset_id,
                       const PresetSwitchState& state
                   )
    {
        // Note:
        // For state.operation == Biz::Preset::PresetDiffOperation::Save,
        // we replace values of *unselected* parameters in the selected preset
        // with values from the original preset.
        // That is why process_operation_from_unsaved_changes() is called with
        // the Save operation *before* updating selected_preset.
        //
        // For state.operation == Biz::Preset::PresetDiffOperation::Transfer,
        // we replace values of *selected* parameters from the previously selected
        // preset into the newly selected preset.
        // That is why process_operation_from_unsaved_changes() is called with
        // the Transfer operation *after* updating selected_preset.

        if (preset_id.kind == Domain::Preset::printer_kind(selected_preset.technology())) {
            for (const auto& [key, item_val] : state.items) {
                selected_preset.printer.config_box().items.opt(key).set(item_val);
            }
            for (const auto& [key, override_val] : state.overrides) {
                // Printer-kind overrides are never disableable, so a disengaged value here is unexpected.
                ASSERT(override_val);
                selected_preset.printer.config_box().overrides.set(key, override_val.value());
            }
        }
        if (preset_id.kind == Domain::Preset::print_kind(selected_preset.technology())) {
            for (const auto& [key, item_val] : state.items) {
                selected_preset.print.config_box().items.opt(key).set(item_val);
            }
            for (const auto& [key, override_val] : state.overrides) {
                // Print-kind overrides are never disableable, so a disengaged value here is unexpected.
                ASSERT(override_val);
                selected_preset.print.config_box().overrides.set(key, override_val.value());
            }
        }
        if (preset_id.kind == Domain::Preset::material_kind(selected_preset.technology())
            && preset_id.id)
        {
            for (const auto& [key, item_val] : state.items) {
                selected_preset.materials[preset_id.id.value()].config_box().items.opt(key).set(
                    item_val
                );
            }
            for (const auto& [key, override_val] : state.overrides) {
                if (override_val) {
                    selected_preset.materials[preset_id.id.value()].config_box().overrides.set(
                        key,
                        override_val.value()
                    );
                } else {
                    selected_preset.materials[preset_id.id.value()].config_box().overrides.disable(
                        key
                    );
                }
            }
        }
        if (preset_id.kind == Domain::Preset::PresetKind::FdmToolPrint && preset_id.id) {
            for (const auto& [key, item_val] : state.items) {
                selected_preset.tools[preset_id.id.value()].config_box().items.opt(key).set(
                    item_val
                );
            }
            for (const auto& [key, override_val] : state.overrides) {
                if (override_val) {
                    selected_preset.tools[preset_id.id.value()].config_box().overrides.set(
                        key,
                        override_val.value()
                    );
                } else {
                    selected_preset.tools[preset_id.id.value()].config_box().overrides.disable(key);
                }
            }
        }

        if (state.operation == Biz::Preset::PresetDiffOperation::Save) {
            // save preset from selected_preset with state.new_preset_name
            KeySet items_to_omit;
            std::ranges::copy(
                state.items | std::views::keys,
                std::inserter(items_to_omit, items_to_omit.begin())
            );
            std::ranges::copy(
                state.overrides | std::views::keys,
                std::inserter(items_to_omit, items_to_omit.begin())
            );
            save_user_preset_internal(
                preset_id.kind,
                preset_id.id.value_or(0),
                items_to_omit,
                state.new_preset_name.value(),
                bag
            );
        }
    };

    if (kind) {
        PresetsSwitchStates::iterator it = find_unsaved_change(operation, kind.value(), tool_id);
        if (it != m_unsaved_changes.end()) {
            // Process current state
            process(selected_preset, it->first, it->second);
            // Remove it from the
            if (operation == PresetDiffOperation::Transfer) {
                bag.add(
                    [kind_val = kind.value(), tool_id, operation, this]()
                    {
                        PresetsSwitchStates::iterator it =
                            find_unsaved_change(operation, kind_val, tool_id);
                        if (it != m_unsaved_changes.end()) {
                            m_unsaved_changes.erase(it);
                        }
                    }
                );
            } else {
                m_unsaved_changes.erase(it);
            }
        }
        return;
    }

    // iterate the unsaved changes and remove them one by one just after completing the processing
    auto it = m_unsaved_changes.begin();
    while (it != m_unsaved_changes.end())
    {
        const auto& preset_id = it->first;
        const auto& state = it->second;
        if (state.operation == operation) {
            process(selected_preset, preset_id, state);
        }
        else {
            ++it;
        }
    }

    bag.add(
        [this, operation]()
        {
            auto it = m_unsaved_changes.begin();
            while (it != m_unsaved_changes.end()) {
                const auto& state = it->second;
                if (state.operation == operation) {
                    it = m_unsaved_changes.erase(it);
                }
                else {
                    ++it;
                }
            }
        }
    );
}


namespace {

void find_preset_usages(
    Domain::Preset::PresetParentPaths& usages,
    const Domain::Preset::SelectedPreset& selected_preset,
    Domain::Preset::PresetKind kind,
    const std::string& preset_id
)
{

    switch (kind) {
    case Domain::Preset::PresetKind::FdmPrinter:
    case Domain::Preset::PresetKind::SlaPrinter:
        if (selected_preset.printer.id == preset_id) {
            usages.emplace_back(selected_preset.hw_config.id);
        }
        break;

    case Domain::Preset::PresetKind::FdmPrint:
    case Domain::Preset::PresetKind::SlaPrint:
        if (selected_preset.print.id == preset_id) {
            usages.emplace_back(selected_preset.hw_config.id, selected_preset.printer.id);
        }
        break;

    case Domain::Preset::PresetKind::FdmToolPrint:
    case Domain::Preset::PresetKind::SlaToolPrint:
        for (size_t i = 0, n = selected_preset.tools.size(); i < n; ++i) {
            if (selected_preset.tools[i].id == preset_id) {
                usages.emplace_back(
                    selected_preset.hw_config.id,
                    selected_preset.printer.id,
                    selected_preset.print.id,
                    i
                );
            }
        }
        break;

    case Domain::Preset::PresetKind::FdmMaterial:
    case Domain::Preset::PresetKind::SlaMaterial:
        for (size_t i = 0, n = selected_preset.materials.size(); i < n; ++i) {
            if (selected_preset.materials[i].id == preset_id) {
                usages.emplace_back(
                    selected_preset.hw_config.id,
                    selected_preset.printer.id,
                    selected_preset.print.id,
                    i
                );
            }
        }
        break;
    }
}

Domain::Preset::PresetParentPaths find_preset_usages(const Domain::Project& project, Domain::Preset::PresetKind kind, const std::string& preset_id)
{
    Domain::Preset::PresetParentPaths ret;
    for (const auto& cc : project.config_containers()) {
        find_preset_usages(ret, cc->selected_preset(), kind, preset_id);
    }

    return ret;
}


bool contains_preset_id(const Domain::Preset::SelectedPreset& selected_preset, Domain::Preset::PresetKind kind, const std::string& preset_id)
{
    switch (kind) {
    case Domain::Preset::PresetKind::FdmPrinter:
    case Domain::Preset::PresetKind::SlaPrinter:
        return selected_preset.printer.id == preset_id;

    case Domain::Preset::PresetKind::FdmPrint:
    case Domain::Preset::PresetKind::SlaPrint:
        return selected_preset.print.id == preset_id;

    case Domain::Preset::PresetKind::FdmToolPrint:
    case Domain::Preset::PresetKind::SlaToolPrint:
        return std::ranges::any_of(selected_preset.tools, [&](const auto& p) -> bool { return p.id == preset_id; });

    case Domain::Preset::PresetKind::FdmMaterial:
    case Domain::Preset::PresetKind::SlaMaterial:
        return std::ranges::any_of(selected_preset.materials, [&](const auto& p) -> bool { return p.id == preset_id; });
    }
    return false;
}

bool contains_preset_id(const Domain::Project& project, Domain::Preset::PresetKind kind, const std::string& preset_id)
{
    return std::ranges::any_of(
        project.config_containers(),
        [&](const auto& cc) -> bool
        { return contains_preset_id(cc.get()->selected_preset(), kind, preset_id); }
    );
}

void drop_preset(
    Domain::Preset::EvaluatedPrinterPresets& presets,
    Domain::Preset::PresetKind kind,
    const std::string& preset_id
)
{
    const bool printer_selected = is_printer(kind);
    if (printer_selected) {
        for (auto& printers : presets | std::views::values) {
            auto it = std::ranges::remove_if(
                printers,
                [&preset_id](const auto& p) { return p.preset.id == preset_id; }
            );
            printers.erase(it.begin(), it.end());
        }
        return;
    }
    const bool print_selected      = is_print(kind);
    const bool tool_print_selected = is_tool_print(kind);
    const bool material_selected   = is_material(kind);
    for (auto& printer : presets | std::views::values | std::views::join) {
        if (print_selected) {
            auto it = std::ranges::remove_if(
                printer.prints,
                [&preset_id](const auto& p) { return p.preset.id == preset_id; }
            );
            printer.prints.erase(it.begin(), it.end());
        } else {
            for (auto& print : printer.prints) {
                if (tool_print_selected) {
                    for (auto& single_tool : print.tools) {
                        auto it = std::ranges::remove_if(
                            single_tool,
                            [&preset_id](const auto& p) { return p.preset.id == preset_id; }
                        );
                        single_tool.erase(it.begin(), it.end());
                    }
                } else if (material_selected) {
                    for (auto& single_tool : print.materials) {
                        auto it = std::ranges::remove_if(
                            single_tool,
                            [&preset_id](const auto& p) { return p.preset.id == preset_id; }
                        );
                        single_tool.erase(it.begin(), it.end());
                    }
                }
            }
        }
    }
}

}

void PresetInteractor::delete_preset(Domain::Preset::PresetKind kind, const std::string& preset_id)
{
    auto& preset_bundle = m_workbench.preset_bundle();
    // 1. for all projects:
    for (const auto& [project_id, project] : m_workbench.projects()) {
        // 1.1. if project uses given preset
        const Domain::Preset::PresetParentPaths usages =
            find_preset_usages(project, kind, preset_id);
        if (usages.empty()) {
            continue;
        }

        // 1.2. copy the preset to project-scope
        auto ctx = get_or_create_project_context(project_id);
        for (const auto& usage : usages) {

            switch (kind) {
            case Domain::Preset::PresetKind::FdmPrinter:
            case Domain::Preset::PresetKind::SlaPrinter: {
                const auto& p = get_printer_preset(project_id, usage.hw_config_id, preset_id).first.get();
                ctx.runtime_presets.printer[usage.hw_config_id].push_back(p);
                break;
            }
            case Domain::Preset::PresetKind::FdmPrint:
            case Domain::Preset::PresetKind::SlaPrint: {
                const auto& p = get_print_preset(
                                    project_id,
                                    usage.hw_config_id,
                                    usage.printer_id.value(),
                                    preset_id
                )
                                    .first.get();
                ctx.runtime_presets.print[{usage.hw_config_id, usage.printer_id.value()}].push_back(
                    p
                );
                break;
            }
            case Domain::Preset::PresetKind::FdmToolPrint:
            case Domain::Preset::PresetKind::SlaToolPrint: {
                const auto used_slots =
                    preset_bundle.get_tool_print_preset_used_slots(
                        usage.hw_config_id,
                        usage.printer_id.value(),
                        usage.print_id.value()
                    );
                for (size_t slot : used_slots) {
                    const auto& p = get_tool_print_preset(
                                        project_id,
                                        usage.hw_config_id,
                                        usage.printer_id.value(),
                                        usage.print_id.value(),
                                        slot,
                                        preset_id
                    )
                                        .first.get();
                    ctx.runtime_presets.add_tool_print(
                        {usage.hw_config_id, usage.printer_id.value(), usage.print_id.value()},
                        get_printer_config(project_id, usage.hw_config_id).first.get(),
                        slot,
                        p
                    );
                }
                break;
            }
            case Domain::Preset::PresetKind::FdmMaterial:
            case Domain::Preset::PresetKind::SlaMaterial: {
                const auto used_slots =
                    preset_bundle.get_material_preset_used_slots(
                        usage.hw_config_id,
                        usage.printer_id.value(),
                        usage.print_id.value()
                    );
                for (size_t slot : used_slots) {
                    const auto& p = get_material_preset(
                                        project_id,
                                        usage.hw_config_id,
                                        usage.printer_id.value(),
                                        usage.print_id.value(),
                                        slot,
                                        preset_id
                    )
                                        .first.get();
                    ctx.runtime_presets.add_material(
                        {usage.hw_config_id, usage.printer_id.value(), usage.print_id.value()},
                        get_printer_config(project_id, usage.hw_config_id).first.get(),
                        slot,
                        p
                    );
                }
                break;
            }

            } // end of switch
        }
    }

    // 2. remove the preset from bundle
    drop_preset(preset_bundle.evaluated_presets, kind, preset_id);
}

PresetInteractorProjectContext& PresetInteractor::get_or_create_project_context(
    Domain::SelectionId project_id
)
{
    auto it = m_project_contexts.find(project_id);
    if (it != m_project_contexts.end())
        return it->second;

    bool _;
    std::tie(it, _) =
        m_project_contexts.emplace(project_id, PresetInteractorProjectContext{project_id});

    return it->second;
}

PresetInteractorConfigContainerContext& PresetInteractor::get_or_create_config_container_context(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id
)
{
    auto& project         = m_workbench.project(project_id);
    auto& project_context = get_or_create_project_context(project_id);
    auto it               = project_context.config_containers.find(config_container_id);
    if (it != project_context.config_containers.end())
        return it->second;

    PresetInteractorConfigContainerContext ccc{config_container_id};
    Domain::ConfigContainer& cc = *ASSERT_VAL(project.find_config_container(config_container_id));

    // TODO: initialize ConfigContainerContext here

    bool _;
    std::tie(it, _) =
        project_context.config_containers.emplace(config_container_id, std::move(ccc));
    return it->second;
}

namespace {
using Domain::Preset::HwPrinterConfig;
using Domain::Preset::HwPrinterConfigs;

const HwPrinterConfig*
find_same_config(const HwPrinterConfigs& bundle_configs, const HwPrinterConfig& hw_config)
{
    auto it = std::ranges::find_if(
        bundle_configs,
        [&hw_config](const HwPrinterConfig& cfg) { return hw_config.has_same_values(cfg); }
    );
    return &*it;
}

template <typename T>
concept HavingPresetWithId = requires(T a) {
    a.preset.id;

    requires std::same_as<std::string, std::remove_cvref_t<decltype(a.preset.id)>>;
};

template <HavingPresetWithId T>
const T* find_by_id(const std::vector<T>& data, const std::string& id)
{
    auto it =
        std::find_if(data.begin(), data.end(), [&id](const T& x) { return x.preset.id == id; });
    return it != data.end() ? &*it : nullptr;
}

void update_features(const Domain::Preset::FeatureDefs& defs, Domain::Preset::FeatureValueMap& features)
{
    for (const auto& [name, def] : defs) {
        if (features.find(name) == features.end()) {
            features[name] = def.default_value;
        }
    }
}

} // namespace

tl::expected<void, std::string> PresetInteractor::update_hw_config_features(
    const Domain::Preset::Bundle& preset_bundle,
    HwPrinterConfig& hw_config
)
{
    const auto vendor_it = preset_bundle.vendor_bundles.find(hw_config.vendor_id);
    if (vendor_it == preset_bundle.vendor_bundles.end()) {
        return tl::unexpected(
            fmt::format(fmt::runtime(_u8L("Unknown vendor: {}")), hw_config.vendor_id)
        );
    }
    const auto& vendor_bundle = vendor_it->second;
    auto vendor_def_it        = vendor_bundle.vendor_data.defs.find(hw_config.technology);
    if (vendor_def_it == vendor_bundle.vendor_data.defs.end()) {
        return tl::unexpected(
            fmt::format(
                fmt::runtime(_u8L("Unsupported technology {} for vendor: {}")),
                magic_enum::enum_name(hw_config.technology),
                hw_config.vendor_id
            )
        );
    }
    const auto& vendor_defs   = vendor_def_it->second;

    const auto printer_it = vendor_defs.printers.find(hw_config.printer_id);
    if (printer_it == vendor_defs.printers.end()) {
        return tl::unexpected(
            fmt::format(
                fmt::runtime(_u8L("Unknown printer id \"{}\" in vendor: {}")),
                hw_config.printer_id,
                hw_config.vendor_id
            )
        );
    }
    const auto& printer_def = printer_it->second;
    update_features(printer_def.features, hw_config.features);
    update_features(vendor_bundle.vendor_data.info.features.printer, hw_config.features);

    for (auto& tool_cfg : hw_config.tools) {
        const auto tool_it = vendor_defs.tools.find(tool_cfg.id);
        if (tool_it == vendor_defs.tools.end()) {
            return tl::unexpected(
                fmt::format(
                    fmt::runtime(_u8L("Unknown tool id \"{}\" in vendor: {}")),
                    tool_cfg.id,
                    hw_config.vendor_id
                )
            );
        }
        const auto& tool_def = tool_it->second;
        update_features(tool_def.features, tool_cfg.features);
        update_features(vendor_bundle.vendor_data.info.features.tool, tool_cfg.features);
    }

    for (auto& feeder_cfg : hw_config.feeders | std::views::values) {
        const auto feeder_it = vendor_defs.feeders.find(feeder_cfg.id);
        if (feeder_it == vendor_defs.feeders.end()) {
            return tl::unexpected(
                fmt::format(
                    fmt::runtime(_u8L("Unknown tool id \"{}\" in vendor: {}")),
                    feeder_cfg.id,
                    hw_config.vendor_id
                )
            );
        }
        const auto& feeder_def = feeder_it->second;
        update_features(feeder_def.features, feeder_cfg.features);
        update_features(vendor_bundle.vendor_data.info.features.feeder, feeder_cfg.features);
    }
    return {};
}

tl::expected<void, std::string>  PresetInteractor::load_selected_preset_from_3mf(
    Domain::SelectionId project_id,
    Domain::Preset::SelectedPreset& selected_preset
)
{
    using Domain::Preset::PresetOrigin;

    auto& pc              = get_or_create_project_context(project_id);
    auto& runtime_presets = pc.runtime_presets;

    bool runtime_presets_evaluation_required = false;
    const auto& preset_bundle = m_workbench.preset_bundle();

    // update origin:
    selected_preset.printer.origin = PresetOrigin::Runtime;
    selected_preset.print.origin   = PresetOrigin::Runtime;
    for (auto& p : selected_preset.tools) {
        p.origin = PresetOrigin::Runtime;
    }
    for (auto& p : selected_preset.materials) {
        p.origin = PresetOrigin::Runtime;
    }

    // 0. Update features according to up-to-date definitions
    auto& hw_config = selected_preset.hw_config;
    auto update_result =
        update_hw_config_features(preset_bundle, hw_config);
    if (!update_result.has_value())
        return update_result;

    // 1. Reconcile HwPrinterConfig
    // deduplicate existing hw configuration (same_values())
    const auto printer_configs_view = get_printer_configs_view(project_id);
    const auto* same_hw_config =
        printer_configs_view.find_with_same_values(selected_preset.hw_config);
    if (same_hw_config != nullptr) {
        selected_preset.hw_config = *same_hw_config;
    } else {
        // check for duplicate id, if conflict update to new ID and update selected_preset with that ID
        if (printer_configs_view.has_conflicting_id(selected_preset.hw_config.id))
            selected_preset.hw_config.id = generate_uuid();
        runtime_presets.printer_configs.emplace(
            std::pair{selected_preset.hw_config.id, selected_preset.hw_config}
        );
        runtime_presets_evaluation_required = true;
    }

    const auto& config_id = selected_preset.hw_config.id;

    // 2. Reconcile PrinterPreset
    // deduplicate existing printer preset (check for config box differences)
    const auto printer_presets_view = get_printer_presets_view(project_id, config_id);
    const Domain::Preset::EvaluatedPrinterPreset::Preset* ep_printer =
        printer_presets_view
            .find_with_same_values(selected_preset.printer);

    if (ep_printer != nullptr) {
        selected_preset.printer = *ep_printer;
    } else {
        // check for duplicate id, if conflict update to new ID and update selected_preset with that ID
        if (printer_presets_view.has_conflicting_id(selected_preset.printer.id)) {
            selected_preset.printer.id = generate_uuid();
        }
        runtime_presets.printer[config_id].push_back(selected_preset.printer);
    }
    const auto& printer_id = selected_preset.printer.id;

    // 3. Reconcile PrintPreset
    // deduplicate existing print preset (check for config box differences)
    const auto print_presets_view = get_print_presets_view(project_id, config_id, printer_id);
    const auto* ep_print = ep_printer == nullptr ?
        nullptr :
        print_presets_view.find_with_same_values(selected_preset.print);
    if (ep_print != nullptr) {
        selected_preset.print = *ep_print;
    } else {
        // check for duplicate id, if conflict update to new ID and update selected_preset with that ID
        if (print_presets_view.has_conflicting_id(selected_preset.print.id)) {
            selected_preset.print.id = generate_uuid();
        }
        const RuntimePresets::HwConfigPrinterKey printer_key{config_id, printer_id};
        runtime_presets.print[printer_key].push_back(selected_preset.print);
    }
    const auto& print_id = selected_preset.print.id;

    // 4. Reconcile ToolPrintPreset
    const RuntimePresets::HwConfingPrinterPrintKey printer_print_key{
        config_id,
        printer_id,
        print_id
    };

    if (selected_preset.hw_config.technology == Domain::PrinterTechnology::FFF) {
        for (size_t tool_index = 0; tool_index < selected_preset.hw_config.tool_count; ++tool_index) {
            auto& selected_tool_print = selected_preset.tools.at(tool_index);
            // deduplicate existing tool_print preset (check for config box differences)
            const auto tool_print_presets_view = get_tool_print_presets_view(
                project_id,
                config_id,
                printer_id,
                print_id,
                tool_index
            );
            const auto* ep_tool_print = ep_print == nullptr ?
                nullptr :
                tool_print_presets_view.find_with_same_values(selected_tool_print);

            if (ep_tool_print != nullptr) {
                selected_tool_print = *ep_tool_print;
            } else {
                // check for duplicate id, if conflict update to new ID and update selected_preset with that ID
                if (tool_print_presets_view.has_conflicting_id(selected_tool_print.id)) {
                    selected_tool_print.id = generate_uuid();
                }
                runtime_presets.add_tool_print(
                    printer_print_key,
                    selected_preset.hw_config,
                    tool_index,
                    selected_tool_print
                );
            }
        }
    }

    // 5. Reconcile ToolPrintPreset
    for (size_t n = selected_preset.hw_config.material_slot_count(), slot_index = 0; slot_index < n;
         ++slot_index)
    {
        auto& selected_material = selected_preset.materials.at(slot_index);

        // deduplicate existing tool_print preset (check for config box differences)
        const auto material_presets_view = get_material_presets_view(
            project_id,
            config_id,
            printer_id,
            print_id,
            slot_index
        );
        const auto* ep_material = ep_print == nullptr ?
            nullptr :
            material_presets_view.find_with_same_values(selected_material);
        if (ep_material != nullptr) {
            selected_material = *ep_material;
        } else {
            // check for duplicate id, if conflict update to new ID and update selected_preset with that ID
            if (material_presets_view.has_conflicting_id(selected_material.id)) {
                selected_material.id = generate_uuid();
            }
            runtime_presets.add_material(
                printer_print_key,
                selected_preset.hw_config,
                slot_index,
                selected_material
            );
        }
    }

    // 6. Insert evaluated presets to runtime
    if (runtime_presets_evaluation_required) {
        auto vb_it = preset_bundle.vendor_bundles.find(selected_preset.hw_config.vendor_id);
        if (vb_it != preset_bundle.vendor_bundles.end()) {
            const auto& vendor_bundle = vb_it->second;
            PresetEvaluator preset_evaluator{vendor_bundle.presets};
            try {
                auto epps             = preset_evaluator.evaluate(selected_preset.hw_config);
                const auto& hw_config = selected_preset.hw_config;
                append_evaluated_presets_to_runtime(runtime_presets, hw_config, epps);
            } catch (const std::exception& e) {
                SPDLOG_ERROR("{}", e.what());
            }
        }
    }

    return {};
}

PresetInteractor::ConstPtrBoolPair<Domain::Preset::HwPrinterConfig>
PresetInteractor::get_printer_config_unsafe(Domain::SelectionId project_id, const std::string& hw_config_id) const
{
    const auto& printer_configs = m_workbench.preset_bundle().printer_configs;
    if (auto it = printer_configs.find(hw_config_id); it != printer_configs.end())
        return {&it->second, false};
    const auto& p   = get_or_fail_project_context(project_id);
    const auto* cfg = p.runtime_presets.find_printer_config_by_id(hw_config_id);

    return {cfg, true};

}

PresetInteractor::ConstRefBoolPair<Domain::Preset::HwPrinterConfig>
PresetInteractor::get_printer_config(
    Domain::SelectionId project_id,
    const std::string& hw_config_id
) const
{
    auto [cfg, runtime] = get_printer_config_unsafe(project_id, hw_config_id);

    ASSERT(cfg != nullptr, hw_config_id);
    return {std::cref(*cfg), runtime};
}

PresetInteractor::ConstPtrBoolPair<Domain::Preset::EvaluatedPrinterPreset::Preset>
PresetInteractor::get_printer_preset_unsafe(
    Domain::SelectionId project_id,
    const std::string& hw_config_id,
    const std::string& printer_preset_id
) const
{
    const auto* printer_preset =
    m_workbench.preset_bundle().find_printer_preset(hw_config_id, printer_preset_id);
    if (printer_preset != nullptr)
        return {&printer_preset->preset, false};

    const auto& p = get_or_fail_project_context(project_id);
    const auto* runtime_printer_preset =
        p.runtime_presets.find_printer_preset_by_id(hw_config_id, printer_preset_id);
    return {runtime_printer_preset, true};
}

PresetInteractor::ConstRefBoolPair<Domain::Preset::EvaluatedPrinterPreset::Preset>
PresetInteractor::get_printer_preset(
    Domain::SelectionId project_id,
    const std::string& hw_config_id,
    const std::string& printer_preset_id
) const
{
    auto [printer_preset, runtime] =
        get_printer_preset_unsafe(project_id, hw_config_id, printer_preset_id);
    ASSERT(printer_preset != nullptr, std::tuple{hw_config_id, printer_preset_id});
    return {std::cref(*printer_preset), runtime};
}

PresetInteractor::ConstPtrBoolPair<Domain::Preset::EvaluatedPrintPreset::Preset>
PresetInteractor::get_print_preset_unsafe(
    Domain::SelectionId project_id,
    const std::string& hw_config_id,
    const std::string& printer_preset_id,
    const std::string& print_id
) const
{
    const auto* printer_preset =
    m_workbench.preset_bundle().find_printer_preset(hw_config_id, printer_preset_id);
    if (printer_preset != nullptr) {
        const auto* print = find_by_id(printer_preset->prints, print_id);
        if (print != nullptr)
            return {&print->preset, false};
    }
    const auto& p = get_or_fail_project_context(project_id);
    const auto* print =
        p.runtime_presets.find_print_preset_by_id({hw_config_id, printer_preset_id}, print_id);
    return {print, true};
}

PresetInteractor::ConstRefBoolPair<Domain::Preset::EvaluatedPrintPreset::Preset>
PresetInteractor::get_print_preset(
    Domain::SelectionId project_id,
    const std::string& hw_config_id,
    const std::string& printer_preset_id,
    const std::string& print_id
) const
{
    const auto [print, runtime] =
        get_print_preset_unsafe(project_id, hw_config_id, printer_preset_id, print_id);
    ASSERT(print != nullptr, std::tuple{hw_config_id, printer_preset_id, print_id});
    return {std::cref(*print), runtime};
}

PresetInteractor::ConstPtrBoolPair<Domain::Preset::EvaluatedToolPrintPreset::Preset>
PresetInteractor::get_tool_print_preset_unsafe(
    Domain::SelectionId project_id,
    const std::string& hw_config_id,
    const std::string& printer_preset_id,
    const std::string& print_preset_id,
    size_t tool_index,
    const std::string& tool_print_preset_id
) const
{
    const auto* printer_preset =
        m_workbench.preset_bundle().find_printer_preset(hw_config_id, printer_preset_id);
    if (printer_preset != nullptr) {
        const auto* print = find_by_id(printer_preset->prints, print_preset_id);
        if (print != nullptr) {
            const auto* tool_print = find_by_id(print->tools[tool_index], tool_print_preset_id);
            if (tool_print != nullptr) {
                return {&tool_print->preset, false};
            }
        }
    }
    const auto& p          = get_or_fail_project_context(project_id);
    const auto* tool_print = p.runtime_presets.find_tool_print_preset_by_id(
        {hw_config_id, printer_preset_id, print_preset_id},
        tool_index,
        tool_print_preset_id
    );
    return {tool_print, true};
}

PresetInteractor::ConstRefBoolPair<Domain::Preset::EvaluatedToolPrintPreset::Preset> PresetInteractor::get_tool_print_preset(
    Domain::SelectionId project_id,
    const std::string& hw_config_id,
    const std::string& printer_preset_id,
    const std::string& print_preset_id,
    size_t tool_index,
    const std::string& tool_print_preset_id
) const
{
    const auto [tool_print, runtime] = get_tool_print_preset_unsafe(
        project_id,
        hw_config_id,
        printer_preset_id,
        print_preset_id,
        tool_index,
        tool_print_preset_id
    );
    ASSERT(
        tool_print != nullptr,
        std::tuple{hw_config_id, printer_preset_id, print_preset_id, tool_print_preset_id}
    );
    return {std::cref(*tool_print), runtime};
}

PresetInteractor::ConstPtrBoolPair<Domain::Preset::EvaluatedMaterialPreset::Preset>
PresetInteractor::get_material_preset_unsafe(
    Domain::SelectionId project_id,
    const std::string& hw_config_id,
    const std::string& printer_preset_id,
    const std::string& print_preset_id,
    size_t slot_index,
    const std::string& material_preset_id
) const
{
    const auto* printer_preset =
        m_workbench.preset_bundle().find_printer_preset(hw_config_id, printer_preset_id);
    if (printer_preset != nullptr) {
        const auto* print = find_by_id(printer_preset->prints, print_preset_id);
        if (print != nullptr) {
            const auto* material = find_by_id(print->materials[slot_index], material_preset_id);
            if (material != nullptr) {
                return {&material->preset, false};
            }
        }
    }
    const auto& p        = get_or_fail_project_context(project_id);
    const auto* material = p.runtime_presets.find_material_preset_by_id(
        {hw_config_id, printer_preset_id, print_preset_id},
        slot_index,
        material_preset_id
    );
    return {material, true};
}

PresetInteractor::ConstRefBoolPair<Domain::Preset::EvaluatedMaterialPreset::Preset>
PresetInteractor::get_material_preset(
    Domain::SelectionId project_id,
    const std::string& hw_config_id,
    const std::string& printer_preset_id,
    const std::string& print_preset_id,
    size_t slot_index,
    const std::string& material_preset_id
) const
{
    const auto [material, runtime] = get_material_preset_unsafe(
        project_id,
        hw_config_id,
        printer_preset_id,
        print_preset_id,
        slot_index,
        material_preset_id
    );
    ASSERT(
        material != nullptr,
        std::tuple{hw_config_id, printer_preset_id, print_preset_id, material_preset_id}
    );
    return {std::cref(*material), runtime};
}

const std::string* PresetInteractor::get_printer_system_preset_id(
    Domain::SelectionId project_id,
    const std::string& hw_config_id,
    const std::string& printer_preset_id
) const
{
    return get_system_counterpart_id(
        get_printer_preset(project_id, hw_config_id, printer_preset_id).first.get()
    );
}

const std::string* PresetInteractor::get_print_system_preset_id(
    Domain::SelectionId project_id,
    const std::string& hw_config_id,
    const std::string& printer_preset_id,
    const std::string& print_id
) const
{
    return get_system_counterpart_id(
        get_print_preset(project_id, hw_config_id, printer_preset_id, print_id).first.get()
    );
}

const std::string* PresetInteractor::get_tool_print_system_preset_id(
    Domain::SelectionId project_id,
    const std::string& hw_config_id,
    const std::string& printer_preset_id,
    const std::string& print_preset_id,
    size_t tool_index,
    const std::string& tool_print_preset_id
) const
{
    const auto& p{get_tool_print_preset(
        project_id,
        hw_config_id,
        printer_preset_id,
        print_preset_id,
        tool_index,
        tool_print_preset_id
    )};
    return get_system_counterpart_id(p.first.get());
}

const std::string* PresetInteractor::get_material_system_preset_id(
    Domain::SelectionId project_id,
    const std::string& hw_config_id,
    const std::string& printer_preset_id,
    const std::string& print_preset_id,
    size_t slot_index,
    const std::string& material_preset_id
    ) const
{
    const auto& p{get_material_preset(
        project_id,
        hw_config_id,
        printer_preset_id,
        print_preset_id,
        slot_index,
        material_preset_id
    )};
    return get_system_counterpart_id(p.first.get());
}

const Domain::Preset::EvaluatedPrinterPreset::Preset* PresetInteractor::get_printer_system_preset(
    Domain::SelectionId project_id,
    const std::string& hw_config_id,
    const std::string& printer_preset_id
) const
{
    const auto* system_preset_id =
        get_printer_system_preset_id(project_id, hw_config_id, printer_preset_id);
    if (system_preset_id == nullptr) {
        return nullptr;
    }

    return get_printer_preset_unsafe(project_id, hw_config_id, *system_preset_id).first;
}

const Domain::Preset::EvaluatedPrintPreset::Preset* PresetInteractor::get_print_system_preset(
    Domain::SelectionId project_id,
    const std::string& hw_config_id,
    const std::string& printer_preset_id,
    const std::string& print_id
) const
{
    const auto* system_preset_id =
        get_print_system_preset_id(project_id, hw_config_id, printer_preset_id, print_id);
    if (system_preset_id == nullptr) {
        return nullptr;
    }
    return get_print_preset_unsafe(project_id, hw_config_id, printer_preset_id, *system_preset_id)
        .first;
}

const Domain::Preset::EvaluatedToolPrintPreset::Preset* PresetInteractor::get_tool_print_system_preset(
    Domain::SelectionId project_id,
    const std::string& hw_config_id,
    const std::string& printer_preset_id,
    const std::string& print_preset_id,
    size_t tool_index,
    const std::string& tool_print_preset_id
) const
{
    const auto* system_preset_id = get_tool_print_system_preset_id(
        project_id,
        hw_config_id,
        printer_preset_id,
        print_preset_id,
        tool_index,
        tool_print_preset_id
    );
    if (system_preset_id == nullptr) {
        return nullptr;
    }
    return get_tool_print_preset_unsafe(
               project_id,
               hw_config_id,
               printer_preset_id,
               print_preset_id,
               tool_index,
               *system_preset_id
    )
        .first;
}

const Domain::Preset::EvaluatedMaterialPreset::Preset* PresetInteractor::get_material_system_preset(
    Domain::SelectionId project_id,
    const std::string& hw_config_id,
    const std::string& printer_preset_id,
    const std::string& print_preset_id,
    size_t slot_index,
    const std::string& material_preset_id
    ) const
{
    const auto* system_preset_id = get_material_system_preset_id(
        project_id,
        hw_config_id,
        printer_preset_id,
        print_preset_id,
        slot_index,
        material_preset_id
    );
    if (system_preset_id == nullptr) {
        return nullptr;
    }
    return get_material_preset_unsafe(
               project_id,
               hw_config_id,
               printer_preset_id,
               print_preset_id,
               slot_index,
               *system_preset_id
    )
        .first;
}

HwPrinterConfigProjectView PresetInteractor::get_printer_configs_view(
    Domain::SelectionId project_id
) const
{
    return {
        m_workbench.preset_bundle(),
        get_or_fail_project_context(project_id).runtime_presets
    };
}

[[nodiscard]] PrinterPresetProjectView PresetInteractor::get_printer_presets_view(
    Domain::SelectionId project_id,
    const std::string& hw_config_id
) const
{
    return {
        m_workbench.preset_bundle(),
        get_or_fail_project_context(project_id).runtime_presets,
        hw_config_id
    };
}

PrintPresetProjectView PresetInteractor::get_print_presets_view(
    Domain::SelectionId project_id,
    const std::string& hw_config_id,
    const std::string& printer_preset_id
) const
{
    return {
        m_workbench.preset_bundle(),
        get_or_fail_project_context(project_id).runtime_presets,
        hw_config_id,
        printer_preset_id
    };
}

ToolPrintPresetProjectView PresetInteractor::get_tool_print_presets_view(
    Domain::SelectionId project_id,
    const std::string& hw_config_id,
    const std::string& printer_preset_id,
    const std::string& print_preset_id,
    size_t tool_index
) const
{
    return {
        m_workbench.preset_bundle(),
        get_or_fail_project_context(project_id).runtime_presets,
        hw_config_id,
        printer_preset_id,
        print_preset_id,
        tool_index
    };
}

MaterialPresetProjectView PresetInteractor::get_material_presets_view(
    Domain::SelectionId project_id,
    const std::string& hw_config_id,
    const std::string& printer_preset_id,
    const std::string& print_preset_id,
    size_t slot_index
) const
{
    return {
        m_workbench.preset_bundle(),
        get_or_fail_project_context(project_id).runtime_presets,
        hw_config_id,
        printer_preset_id,
        print_preset_id,
        slot_index
    };
}

void PresetInteractor::update_print_tool_cbi(
    Domain::Preset::SelectedPreset& selected_preset,
    Domain::SelectionId config_container_id
)
{
    std::vector<Domain::ConfigBox*> tool_cbs;
    tool_cbs.reserve(selected_preset.tools.size());
    std::transform(
        selected_preset.tools.begin(),
        selected_preset.tools.end(),
        std::back_inserter(tool_cbs),
        [](Domain::Preset::EvaluatedToolPrintPreset::Preset& preset)
        { return &preset.config_box(); }
    );

    const auto& [print_preset_ref, b] = get_print_preset(
        selected_preset.hw_config.id,
        selected_preset.printer.id,
        selected_preset.print.id
    );

    std::vector<const Domain::ConfigBox*> original_tool_cbs;
    original_tool_cbs.reserve(selected_preset.tools.size());
    std::vector<const Domain::Preset::EvaluatedToolPrintPreset::Preset*> tools;
    for (size_t i = 0, n = selected_preset.tools.size(); i < n; i++) {
        const auto& [tool_preset_ref, b] = get_tool_print_preset(
            selected_preset.hw_config.id,
            selected_preset.printer.id,
            selected_preset.print.id,
            i,
            selected_preset.tools[i].id
        );
        original_tool_cbs.emplace_back(&tool_preset_ref.get().config_box());
    }

    m_print_tool_cbi_accessor.set_sources(
        m_selected_project_id,
        config_container_id,
        selected_preset,
        tool_cbs,
        &print_preset_ref.get().config_box(),
        original_tool_cbs
    );
}

} // namespace Slic3r::Biz::Preset
