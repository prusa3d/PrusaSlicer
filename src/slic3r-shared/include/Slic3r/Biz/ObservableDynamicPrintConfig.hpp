#pragma once

#include "Observable.hpp"

#include <libslic3r/PrintConfig.hpp>

namespace Slic3r::Biz {

class ObservableDynamicPrintConfig : public ObservableValue<Slic3r::DynamicPrintConfig>
{
public:
    using ObservableValue<Slic3r::DynamicPrintConfig>::get;
    using ObservableValue<Slic3r::DynamicPrintConfig>::set;

    void set(const std::string &key, Slic3r::ConfigOption *value) {
        m_value.set_key_value(key, value);
        this->notify();
    }

    [[nodiscard]] const Slic3r::ConfigOption *get(const std::string &key) const {
        return m_value.optptr(key);
    }
    [[nodiscard]] const Slic3r::DynamicPrintConfig &get() const override { return m_value; }
};

}
