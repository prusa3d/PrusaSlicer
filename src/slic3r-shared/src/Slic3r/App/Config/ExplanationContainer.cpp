#include "Slic3r/App/Config/ExplanationContainer.hpp"

#include "Slic3r/Domain/Config.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/PrintToolItem.hpp"

#include "Slic3r/App/Config/ConfigItemUtils.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"

#include <fmt/format.h>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

static float measure_widest(const std::string& left, const std::string& right)
{
    const ImVec2 left_size  = ImGui::CalcTextSize(left.c_str());
    const ImVec2 right_size = ImGui::CalcTextSize(right.c_str());
    return std::max(left_size.x, right_size.x);
}

ExplanationView::ExplanationView(size_t index, const ExplanationPart& data) :
    Biz::DataObserver<ExplanationPart>(index, data),
    Text(std::string{})
{
    set_flex_shrink(0);
}

void ExplanationView::on_data_update()
{
    set_text(m_state->text);
    set_text_color(m_state->color);
    set_min_width(m_state->min_width);
    set_align({m_state->text_align, AlignV::Center});
}

ExplanationContainer::ExplanationContainer(Biz::ProjectInteractor& project_interactor) :
    m_project_interactor(project_interactor),
    m_explanation_list_labels(std::make_shared<Biz::ObservableList<ExplanationPart>>()),
    m_explanation_list(std::make_shared<Biz::ObservableList<ExplanationPart>>())
{
    set_orientation(Orientation::Vertical);
    set_gap(10_fpx);
    set_visible(false);
    set_padding({15_fpx, 10_fpx});

    const ImColor color = m_theme->color_imgui(Platform::Color::AccentSecondary);
    const ImColor background_color{color.Value.x, color.Value.y, color.Value.z, 0.25};

    set_fill(background_color);
    set_rounding(0);

    Item* head_row = emplace_back<Item>();
    head_row->set_orientation(Orientation::Horizontal);
    head_row->set_gap(15_fpx);
    head_row->set_align_items(YGAlignCenter);

    Icon* robot = head_row->emplace_back<Icon>(Render::Icon::Robot);
    robot->set_width(16_fpx);
    robot->set_height(16_fpx);
    robot->set_tint(color);

    Text* header =
        head_row->emplace_back<Text>(Biz::_u8L("Conflict resolved"), Render::ImguiFontType::Bold);
    header->set_text_color(color);

    Item* content = emplace_back<Item>();
    content->set_orientation(Orientation::Vertical);
    content->set_gap(10_fpx);

    m_explanation_label = content->emplace_back<Text>(std::string{});
    m_explanation_label->set_wrap_mode(Text::WrapMode::Wrap);
    m_explanation_label->set_text_color(color);

    m_formula_background = content->emplace_back<Rectangle>();
    m_formula_background->set_fill(background_color);
    m_formula_background->set_orientation(Orientation::Horizontal);
    m_formula_background->set_rounding(0);
    m_formula_background->set_padding({15_fpx, 10_fpx});
    m_formula_background->set_gap(5);

    m_explanation_labels_list_view = m_formula_background->emplace_back<ExplanationListView>();
    m_explanation_labels_list_view->set_orientation(Orientation::Vertical);
    m_explanation_labels_list_view->set_gap(5);
    m_explanation_labels_list_view->set_source_list(m_explanation_list_labels.get());
    m_explanation_labels_list_view->set_flex_grow(1);
    m_explanation_labels_list_view->set_justify_content(YGJustifyFlexEnd);

    m_explanation_list_view = m_formula_background->emplace_back<ExplanationListView>();
    m_explanation_list_view->set_orientation(Orientation::Vertical);
    m_explanation_list_view->set_gap(5);
    m_explanation_list_view->set_source_list(m_explanation_list.get());
    m_explanation_list_view->set_flex_grow(1);
}

void ExplanationContainer::update_explanation(const Biz::PrintToolItem& print_tool_item)
{
    bool label_visible  = false;
    bool labels_visible = false;
    bool values_visible = false;

    const ImColor text_color = m_theme->color_imgui(Platform::Color::Text);
    const Domain::CompatibilityRule compatibility_rule =
        print_tool_item.print_item->compatibility_rule();
    if (print_tool_item.print_item->def().compatibility_rule != Domain::CompatibilityRule::Undefined
        && print_tool_item.value.second)
    {
        if (compatibility_rule == Domain::CompatibilityRule::IgnoreOverrides) {
            m_explanation_label->set_text(
                fmt::format(
                    fmt::runtime(
                        Biz::_u8L(
                            "This configuration item requires same value each tool, this is currently "
                            "not true and therefore we have selected {} value to be applied."
                        )
                    ),
                    ConfigItemUtils::config_item_to_string(
                        *print_tool_item.print_item,
                        print_tool_item.value.first
                    )
                )
            );
            label_visible = true;
        } else {
            // construct new explanation list
            std::vector<ExplanationPart> list_labels, list_values;
            list_labels.reserve(16);
            list_values.reserve(16);

            std::string explanation_text =
                Biz::_u8L("The parameter must result in a single value.") + " ";

            ExplanationPart function_part;
            switch (compatibility_rule) {
            case Domain::CompatibilityRule::Average:
                function_part = ExplanationPart{"Average", text_color, 0, AlignH::Right};
                explanation_text += Biz::_u8L("Averaging");
                break;
            case Domain::CompatibilityRule::Max:
                function_part = ExplanationPart{"Max", text_color, 0, AlignH::Right};
                explanation_text += Biz::_u8L("Maximazing");
                break;
            case Domain::CompatibilityRule::Min:
                function_part = ExplanationPart{"Min", text_color, 0, AlignH::Right};
                explanation_text += Biz::_u8L("Minifying");
                break;
            default:
                PANIC("Unsuported compability rule type");
                break;
            }
            explanation_text += " " + Biz::_u8L("rule has been applied");
            m_explanation_label->set_text(explanation_text);
            label_visible = true;

            const std::vector<Domain::ColorRGB> colors =
                m_project_interactor.project_settings_interactor().get_colors(
                    m_project_interactor.selected_config_container_id()
                );

            for (size_t index = 0; index < print_tool_item.tool_overrides.size(); ++index) {
                const Domain::ConfigItem* tool_override = print_tool_item.tool_overrides.at(index);
                if (!print_tool_item.shared_context.extruder_candidates.empty()
                    && !print_tool_item.shared_context.extruder_candidates.contains(index))
                {
                    continue;
                }

                ImColor color;
                if (colors.size() > index) {
                    const Domain::ColorRGB& color_domain = colors[index];
                    color = {color_domain.r(), color_domain.g(), color_domain.b()};
                } else {
                    m_project_interactor.project_settings_interactor().palette_color(index);
                }

                const std::string item_label = Biz::_u8L("Tool") + " " + std::to_string(index + 1);
                const std::string item_value =
                    ConfigItemUtils::config_item_to_string(*tool_override);
                list_labels.emplace_back(ExplanationPart{item_label, color, 0, AlignH::Right});
                list_values.emplace_back(ExplanationPart{item_value, color});
            }

            list_labels.push_back(ExplanationPart{"————————", text_color, 0, AlignH::Right});
            list_values.push_back(ExplanationPart{"————————", text_color, 0, AlignH::Left});

            const std::string item_label = Biz::_u8(print_tool_item.print_item->def().label);
            const std::string item_value = ConfigItemUtils::config_item_to_string(
                *print_tool_item.print_item,
                print_tool_item.value.first
            );

            list_labels.push_back(function_part);
            list_values.push_back(ExplanationPart{item_value, text_color});

            auto reset_if_different =
                [](std::vector<ExplanationPart>&& new_list,
                   Biz::UnsharedPointer<Biz::ObservableList<ExplanationPart>>& list) -> void
            {
                bool replace = false;
                if (list->size() != new_list.size()) {
                    replace = true;
                } else {
                    for (size_t index = 0; index < new_list.size(); ++index) {
                        if (list->at(index).text != new_list.at(index).text) {
                            replace = true;
                            break;
                        }
                    }
                }

                if (replace) {
                    list->reset(std::move(new_list));
                }
            };

            reset_if_different(std::move(list_labels), m_explanation_list_labels);
            reset_if_different(std::move(list_values), m_explanation_list);

            labels_visible = true;
            values_visible = true;
        }
    }

    m_explanation_label->set_visible(label_visible);
    m_explanation_list_view->set_visible(values_visible);
    m_explanation_labels_list_view->set_visible(labels_visible);
    m_formula_background->set_visible(
        m_explanation_list_view->is_self_visible()
        || m_explanation_labels_list_view->is_self_visible()
    );
    set_visible(
        m_explanation_label->is_self_visible()
        || m_explanation_labels_list_view->is_self_visible()
        || m_explanation_list_view->is_self_visible()
    );
}

} // namespace Slic3r::App
