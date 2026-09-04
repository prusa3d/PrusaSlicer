#pragma once
#include <array>
#include <ranges>
#include <utility>
#include <range/v3/view/concat.hpp>
#include <range/v3/view/map.hpp>
#include <range/v3/view/transform.hpp>
#include <range/v3/algorithm/find_if.hpp>

#include "Slic3r/Domain/Preset/Bundle.hpp"
#include "Slic3r/Biz/Preset/PresetInteractorProjectContext.hpp"

namespace Slic3r::Biz::Preset {

namespace Details {
template <typename T, bool second_value>
auto view_pack_as_pair()
{
    return ranges::views::transform(
        [](const T& v) -> std::pair<std::reference_wrapper<const T>, bool>
        { return {v, second_value}; }
    );
}

template <typename Value>
const Value* find_with_same_value(const auto items, const Value& v)
{
    const auto it = ranges::find_if(
        items,
        [&v](const auto& pair) { return pair.first.get().has_same_values(v); }
    );

    return it == items.end() ? nullptr : &(*it).first.get();
}

bool has_conflicting_id(const auto items, const std::string& id)
{
    const auto it = ranges::find_if(
        items,
        [&id](const auto& pair) { return pair.first.get().id == id; }
    );

    return it != items.end();
}

} // namespace Details

/**
 * @brief Unified view at hw printer configs either loaded from preset_bundle
 * or loaded into runtime as part of the project. Use `items()` function to get for-range friendly
 * iterator over items of `const HwPrinterConfig&` type.
 */
class HwPrinterConfigProjectView
{
public:
    using Value = Domain::Preset::HwPrinterConfig;

    HwPrinterConfigProjectView(const Domain::Preset::Bundle& m_bundle, const RuntimePresets& m_presets) :
        m_bundle(m_bundle),
        m_runtime(m_presets)
    {}

    /**
     * @brief Get iterator-like view over printer config.
     * @return For-range iterable of `std::pair<std::ref_wrapper<HwPrinterConfig>, bool>`
     * where the bool indicates if it is a runtime preset.
     *
     * Intended usage
     * @code{.cpp}
     * HwPrinterConfigProjectView view =  ...;
     * for (const auto [hw_config_ref, is_runtime] : view.items()) {
     *     const auto& hw_config = hw_config_ref.get();
     *     // do stuff with hw_config and is_runtime
     * }
     * @endcode
     */
    [[nodiscard]] auto items() const
    {
        return ranges::views::concat(
            ranges::views::values(m_bundle.printer_configs) | Details::view_pack_as_pair<Value, false>(),
            ranges::views::values(m_runtime.printer_configs) | Details::view_pack_as_pair<Value, true>()
        );
    }

    [[nodiscard]] const Value* find_with_same_values(const Value& v) const
    {
        return Details::find_with_same_value(items(), v);
    }

    [[nodiscard]] bool has_conflicting_id(const std::string& id) const
    {
        return Details::has_conflicting_id(items(), id);
    }

private:
    const Domain::Preset::Bundle& m_bundle;
    const RuntimePresets& m_runtime;
};

/**
 * @brief Unified view at printer presets either loaded from preset_bundle
 * or loaded into runtime as part of the project. Use `items()` function to get for-range friendly
 * iterator over items of `const EvaluatedPrinterPreset::Preset&` type.
 */
class PrinterPresetProjectView
{
public:
    using ValueParent = Domain::Preset::EvaluatedPrinterPreset;
    using Value = Domain::Preset::EvaluatedPrinterPreset::Preset;

    PrinterPresetProjectView(const Domain::Preset::Bundle& bundle, const RuntimePresets& presets, std::string  hw_config_id) :
        m_bundle(bundle),
        m_runtime(presets),
        m_hw_config_id(std::move(hw_config_id))
    {}

    /**
     * @brief Get iterator-like view over printer presets.
     * @return For-range iterable of `std::pair<std::ref_wrapper<EvaluatedPrinterPreset::Preset>, bool>`
     * where the bool indicates if it is a runtime preset.
     *
     * Intended usage
     * @code{.cpp}
     * PrinterPresetProjectView view =  ...;
     * for (const auto [printer_preset_ref, is_runtime] : view.items()) {
     *     const auto& printer_preset = printer_preset.get();
     *     // do stuff with printer_preset and is_runtime
     * }
     * @endcode
     */
    [[nodiscard]] auto items() const
    {
        auto bundle_it = m_bundle.evaluated_presets.find(m_hw_config_id);
        auto runtime_it = m_runtime.printer.find(m_hw_config_id);

        auto bundle_span = bundle_it == m_bundle.evaluated_presets.end() ? std::span<const ValueParent>{} : bundle_it->second;
        auto runtime_span = runtime_it == m_runtime.printer.end() ? std::span<const Value>{} : runtime_it->second;

        auto bundle_view = bundle_span | ranges::views::transform(&Domain::Preset::EvaluatedPrinterPreset::preset);

        return ranges::views::concat(
            bundle_view | Details::view_pack_as_pair<Value, false>(),
            runtime_span | Details::view_pack_as_pair<Value, true>()
        );
    }

    [[nodiscard]] const Value* find_with_same_values(const Value& v) const
    {
        return Details::find_with_same_value(items(), v);
    }

    [[nodiscard]] bool has_conflicting_id(const std::string& id) const
    {
        return Details::has_conflicting_id(items(), id);
    }

private:
    const Domain::Preset::Bundle& m_bundle;
    const RuntimePresets& m_runtime;
    const std::string m_hw_config_id;
};


/**
 * @brief Unified view at print presets either loaded from preset_bundle
 * or loaded into runtime as part of the project. Use `items()` function to get for-range friendly
 * iterator over items of `const EvaluatedPrintPreset::Preset&` type.
 */
class PrintPresetProjectView
{
public:
    using ValueParent = Domain::Preset::EvaluatedPrintPreset;
    using Value = Domain::Preset::EvaluatedPrintPreset::Preset;

    PrintPresetProjectView(const Domain::Preset::Bundle& bundle, const RuntimePresets& presets, std::string  hw_config_id, std::string  printer_id) :
        m_bundle(bundle),
        m_runtime(presets),
        m_hw_config_id(std::move(hw_config_id)),
        m_printer_id(std::move(printer_id))
    {}

    /**
     * @brief Get iterator-like view over print presets.
     * @return For-range iterable of `std::pair<std::ref_wrapper<EvaluatedPrintPreset::Preset>, bool>`
     * where the bool indicates if it is a runtime preset.
     *
     * Intended usage
     * @code{.cpp}
     * PrintPresetProjectView view =  ...;
     * for (const auto [print_preset_ref, is_runtime] : view.items()) {
     *     const auto& print_preset = print_preset.get();
     *     // do stuff with print_preset and is_runtime
     * }
     * @endcode
     */
    [[nodiscard]] auto items() const
    {
        auto bundle_printer = m_bundle.find_printer_preset(m_hw_config_id, m_printer_id);
        auto runtime_it = m_runtime.print.find({m_hw_config_id, m_printer_id});

        auto bundle_span = bundle_printer == nullptr ? std::span<const ValueParent>{} : bundle_printer->prints;
        auto runtime_span = runtime_it == m_runtime.print.end() ? std::span<const Value>{} : runtime_it->second;

        auto bundle_view = bundle_span | ranges::views::transform(&Domain::Preset::EvaluatedPrintPreset::preset);

        return ranges::views::concat(
            bundle_view | Details::view_pack_as_pair<Value, false>(),
            runtime_span | Details::view_pack_as_pair<Value, true>()
        );
    }

    [[nodiscard]] const Value* find_with_same_values(const Value& v) const
    {
        return Details::find_with_same_value(items(), v);
    }

    [[nodiscard]] bool has_conflicting_id(const std::string& id) const
    {
        return Details::has_conflicting_id(items(), id);
    }

private:
    const Domain::Preset::Bundle& m_bundle;
    const RuntimePresets& m_runtime;
    const std::string m_hw_config_id;
    const std::string m_printer_id;
};

/**
 * @brief Unified view at tool-print presets either loaded from preset_bundle
 * or loaded into runtime as part of the project. Use `items()` function to get for-range friendly
 * iterator over items of `const EvaluatedToolPrintPreset::Preset&` type.
 */
class ToolPrintPresetProjectView
{
public:
    using ValueParent = Domain::Preset::EvaluatedToolPrintPreset;
    using Value = Domain::Preset::EvaluatedToolPrintPreset::Preset;
    using IteratedValue = std::pair<std::reference_wrapper<const Value>, bool>;

    ToolPrintPresetProjectView(const Domain::Preset::Bundle& bundle, const RuntimePresets& presets, std::string hw_config_id, std::string printer_id, std::string print_id, size_t tool_index) :
        m_bundle(bundle),
        m_runtime(presets),
        m_hw_config_id(std::move(hw_config_id)),
        m_printer_id(std::move(printer_id)),
        m_print_id(std::move(print_id)),
        m_tool_index(tool_index)
    {}

    /**
     * @brief Get iterator-like view over tool-print presets.
     * @return For-range iterable of `std::pair<std::ref_wrapper<EvaluatedToolPrintPreset::Preset>, bool>`
     * where the bool indicates if it is a runtime preset.
     *
     * Intended usage
     * @code{.cpp}
     * ToolPrintPresetProjectView view =  ...;
     * for (const auto [tool_print_preset_ref, is_runtime] : view.items()) {
     *     const auto& tool_print_preset = tool_print_preset.get();
     *     // do stuff with tool_print_preset and is_runtime
     * }
     * @endcode
     */
    [[nodiscard]] auto items() const
    {
        auto bundle_printer = m_bundle.find_printer_preset(m_hw_config_id, m_printer_id);
        auto bundle_print = bundle_printer ? bundle_printer->find_print_preset_by_id(m_print_id) : nullptr;
        auto runtime_it = m_runtime.tool_print.find({m_hw_config_id, m_printer_id, m_print_id});

        auto bundle_span = bundle_print == nullptr ? std::span<const ValueParent>{} : bundle_print->tools.at(m_tool_index);
        auto runtime_span = runtime_it == m_runtime.tool_print.end() ? std::span<const Value>{} : runtime_it->second.at(m_tool_index);

        auto bundle_view = bundle_span | ranges::views::transform(&Domain::Preset::EvaluatedToolPrintPreset::preset);

        return ranges::views::concat(
            bundle_view | Details::view_pack_as_pair<Value, false>(),
            runtime_span | Details::view_pack_as_pair<Value, true>()
        );
    }

    [[nodiscard]] const Value* find_with_same_values(const Value& v) const
    {
        return Details::find_with_same_value(items(), v);
    }

    [[nodiscard]] bool has_conflicting_id(const std::string& id) const
    {
        return Details::has_conflicting_id(items(), id);
    }

private:
    const Domain::Preset::Bundle& m_bundle;
    const RuntimePresets& m_runtime;
    const std::string m_hw_config_id;
    const std::string m_printer_id;
    const std::string m_print_id;
    size_t m_tool_index;
};

/**
 * @brief Unified view at material presets either loaded from preset_bundle
 * or loaded into runtime as part of the project. Use `items()` function to get for-range friendly
 * iterator over items of `const EvaluatedMaterialPreset::Preset&` type.
 */
class MaterialPresetProjectView
{
public:
    using ValueParent = Domain::Preset::EvaluatedMaterialPreset;
    using Value = Domain::Preset::EvaluatedMaterialPreset::Preset;

    MaterialPresetProjectView(const Domain::Preset::Bundle& bundle, const RuntimePresets& runtime, std::string hw_config_id, std::string printer_id, std::string print_id, size_t slot_index) :
        m_bundle(bundle),
        m_runtime(runtime),
        m_hw_config_id(std::move(hw_config_id)),
        m_printer_id(std::move(printer_id)),
        m_print_id(std::move(print_id)),
        m_slot_index(slot_index)
    {}

    /**
     * @brief Get iterator-like view over material presets.
     * @return For-range iterable of `std::pair<std::ref_wrapper<MaterialPrintPreset::Preset>, bool>`
     * where the bool indicates if it is a runtime preset.
     *
     * Intended usage
     * @code{.cpp}
     * MaterialPresetProjectView view =  ...;
     * for (const auto [material_preset_ref, is_runtime] : view.items()) {
     *     const auto& material_preset = material_preset.get();
     *     // do stuff with material_preset and is_runtime
     * }
     * @endcode
     */
    [[nodiscard]] auto items() const
    {
        const auto bundle_printer = m_bundle.find_printer_preset(m_hw_config_id, m_printer_id);
        const auto bundle_print = bundle_printer ? bundle_printer->find_print_preset_by_id(m_print_id) : nullptr;
        const auto runtime_it = m_runtime.material.find({m_hw_config_id, m_printer_id, m_print_id});

        const auto bundle_span = bundle_print == nullptr ? std::span<const ValueParent>{} : bundle_print->materials.at(m_slot_index);
        const auto runtime_span = runtime_it == m_runtime.material.end() ? std::span<const Value>{} : runtime_it->second.at(m_slot_index);

        auto bundle_view = bundle_span | ranges::views::transform(&Domain::Preset::EvaluatedMaterialPreset::preset);

        return ranges::views::concat(
            bundle_view | Details::view_pack_as_pair<Value, false>(),
            runtime_span | Details::view_pack_as_pair<Value, true>()
        );
    }

    [[nodiscard]] const Value* find_with_same_values(const Value& v) const
    {
        return Details::find_with_same_value(items(), v);
    }

    [[nodiscard]] bool has_conflicting_id(const std::string& id) const
    {
        return Details::has_conflicting_id(items(), id);
    }

private:
    const Domain::Preset::Bundle& m_bundle;
    const RuntimePresets& m_runtime;
    const std::string m_hw_config_id;
    const std::string m_printer_id;
    const std::string m_print_id;
    size_t m_slot_index;
};
}
