#pragma once

#include "Slic3r/App/Yoga/Dialog.hpp"
#include "Slic3r/Domain/ConfigValue.hpp"

#include <imgui.h>

#include <optional>
#include <string>
#include <vector>

namespace Slic3r::Biz {
class ProjectInteractor;
}

namespace Slic3r::Domain {
struct ConfigItemDef;
}

namespace Slic3r::App {

class Navigator;
class ValueTable;

class EffectiveValuesDialog : public Yoga::Dialog
{
public:
    EffectiveValuesDialog(Biz::ProjectInteractor& project_interactor, Navigator& navigator);

    void set_key(const std::optional<std::string>& key);

    void update_effective_value_table(const std::string& key);

    void clear();

    void close_action() override;

private:
    std::vector<ImColor> slot_colors(size_t tool_count) const;
    std::string macro_override_label(const std::string& key) const;

    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;
    std::optional<std::string> m_key;

    Yoga::Text* m_title{nullptr};
    Yoga::Text* m_description{nullptr};
    ValueTable* m_table{nullptr};
};

} // namespace Slic3r::App
