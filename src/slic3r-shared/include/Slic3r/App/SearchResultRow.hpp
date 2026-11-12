#pragma once

#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/App/Yoga/RectangleButton.hpp"
#include "Slic3r/Biz/DataObserver.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class Icon;
class Text;
class ButtonGroup;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class Navigator;

class SearchResultRow : public Biz::DataObserver<Domain::ConfigItem>, public Yoga::RectangleButton
{
public:
    explicit SearchResultRow(
        size_t index,
        const Domain::ConfigItem& data,
        Navigator& navigator,
        Biz::ProjectInteractor& project_interactor,
        std::weak_ptr<Yoga::ButtonGroup> button_group
    );
    ~SearchResultRow();

protected:
    void on_data_update() override;

    void hovered_updated_internal() override;

    void action_internal() override;

private:
    Navigator& m_navigator;
    Biz::ProjectInteractor& m_project_interactor;
    std::weak_ptr<Yoga::ButtonGroup> m_button_group;

    Yoga::Icon* m_icon{nullptr};
    Yoga::Text* m_text_category{nullptr};
    Yoga::Text* m_text_label{nullptr};
};

} // namespace Slic3r::App
