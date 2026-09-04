#pragma once

#include "Slic3r/App/Yoga/Dialog.hpp"
#include "Slic3r/Domain/ConfigDef.hpp"

#include <vector>

namespace Slic3r::Biz {
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::Domain {
struct ConfigBox;
} // namespace Slic3r::Domain

namespace Slic3r::App::Yoga {
class ScrollArea;
class StackLayout;
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Plater {

/**
 * @deprecated this class will be replaced by OverrideSettingsDialog
 */
class LayerRangeSettingsDialog : public Yoga::Dialog
{
public:
    explicit LayerRangeSettingsDialog(Biz::IConfigBoxSetter* setter);

    void set_config_box(const Domain::ConfigBox* config_box);
    void open_at_category(Domain::ConfigItemDef::Category category);
    void clear_settings();

private:
    void on_about_to_show() override;

    void init_categories_page();
    void init_settings_page();
    void select_category(Domain::ConfigItemDef::Category category);
    void create_settings_page_for_category(Domain::ConfigItemDef::Category category);

    Biz::IConfigBoxSetter* m_config_box_setter{nullptr};
    const Domain::ConfigBox* m_config_box{nullptr};

    Yoga::StackLayout* m_stack_layout{nullptr};
    Yoga::ScrollArea* m_settings_scroll{nullptr};
    Yoga::Text* m_options_category_text{nullptr};

    std::vector<Yoga::Item*> m_setting_rows;
};

} // namespace Slic3r::App::Plater
