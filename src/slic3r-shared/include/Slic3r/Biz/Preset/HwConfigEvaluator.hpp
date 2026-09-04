#pragma once
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Biz/Expr/Eval.hpp"
#include "Slic3r/Domain/Preset/SourceLocatedExpr.hpp"

namespace Slic3r::Biz::Preset {

template <typename T>
concept Conditional = requires(T a)
{
    {a.condition} -> std::same_as<std::optional<Domain::Preset::SourceLocatedExpr>&>;
};

template <Conditional T>
class ConfigIterator;

template <Conditional T>
class ConfigIterator
{
public:
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = T;
    using pointer = const value_type*;
    using reference = const value_type&;

    ConfigIterator(const ConfigIterator&) = default;

    ConfigIterator& operator++()
    {
        ASSERT(is_valid());
        advance_filtered();
        return *this;
    }

    ConfigIterator operator++(int)
    {
        ASSERT(is_valid());
        ConfigIterator tmp = *this;
        advance_filtered();
        return tmp;
    }

    bool operator==(const ConfigIterator& other) const
    {
        return m_it == other.m_it;
    }

    bool operator!=(const ConfigIterator& other) const
    {
        return m_it != other.m_it;
    }

    reference operator*() const { return m_it->second; }
    pointer operator->() const { return &m_it->second; }

    bool is_valid() const { return m_it != m_container.end(); }

    ConfigIterator begin() const
    {
        ConfigIterator ret = *this;
        ret.reset();
        return ret;
    }

    ConfigIterator end() const
    {
        ConfigIterator ret = *this;
        ret.invalidate();
        return ret;
    }


private:
    using Container = std::map<std::string, T>;
    friend class HwConfigEvaluator;

    ConfigIterator(const Container& container, const Expr::Eval& eval, Expr::ValueMap&& values)
        : m_vars(std::move(values)), m_eval(eval), m_container(container), m_it(m_container.begin())
    {
        ensure_condition_met();
    }

    void reset()
    {
        m_it = m_container.begin();
        ensure_condition_met();
    }
    void invalidate() { m_it = m_container.end(); }

    void advance()
    {
        ASSERT(is_valid());
        ++m_it;
    }

    void advance_filtered()
    {
        advance();
        ensure_condition_met();
    }

    void ensure_condition_met()
    {
        for (;;) {
            if (!is_valid()) {
                return;
            }
            const auto& el = m_it->second;
            if (!el.condition.has_value()) {
                return;
            }
            const auto result = m_eval.eval(el.condition.value().value, m_vars);
            if (boost::get<bool>(result)) {
                return;
            }
            advance();
        }
    }
private:
    Expr::ValueMap m_vars;
    const Expr::Eval& m_eval;
    const Container& m_container;
    typename Container::const_iterator m_it;
};


using HwToolConfigIterator = ConfigIterator<Domain::Preset::HwToolConfigDef>;
using HwFeederConfigIterator = ConfigIterator<Domain::Preset::HwFeederConfigDef>;
using HwSheetConfigIterator = ConfigIterator<Domain::Preset::HwSheetConfigDef>;


class HwConfigEvaluator
{
public:
    HwToolConfigIterator iterate_tools(const Domain::Preset::HwPrinterConfig& printer, const HwToolConfigIterator::Container& tools) const;
    HwFeederConfigIterator iterate_feeders(const Domain::Preset::HwPrinterConfig& printer, const Domain::Preset::HwToolConfig& tool, const HwFeederConfigIterator::Container& feeders) const;
    HwSheetConfigIterator iterate_sheets(const Domain::Preset::HwPrinterConfig& printer, const HwSheetConfigIterator::Container& sheets) const;

    Domain::Preset::HwPrinterConfig create_printer_config(const Domain::Preset::HwPrinterConfigTemplate& templ, const Domain::Preset::VendorData& vendor_data) const;
    void set_debug_output(const Expr::Eval::Logger& logger);

private:
    const Domain::Preset::HwSheetConfigDef* first_compatible_sheet(const Domain::Preset::HwPrinterConfig& printer, const HwSheetConfigIterator::Container& sheets) const;
private:
    Expr::Eval m_eval;
};

Domain::Preset::HwToolConfig
from_def(const Domain::Preset::VendorData& vendor_data, const Domain::Preset::HwToolConfigDef& def, std::optional<Domain::Preset::FeatureValueMap> template_overrides = std::nullopt);

Domain::Preset::HwSheetConfig
from_def(const Domain::Preset::VendorData& vendor_data, const Domain::Preset::HwSheetConfigDef& def);

Domain::Preset::HwPrinterConfig from_def(
    const Domain::Preset::VendorData& vendor_data,
    const Domain::Preset::HwPrinterConfigDef& printer_def
);

Domain::Preset::HwPrinterConfig new_scratch_config(
    Domain::PrinterTechnology technology,
    const std::string& name,
    size_t tool_count
);
}
