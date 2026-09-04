#pragma once

#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/ObservableList.hpp"

#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
struct PrintToolItem;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class ScrollArea;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

struct ExplanationPart
{
    std::string text;
    ImColor color;
    float min_width{0};
    Yoga::AlignH text_align{Yoga::AlignH::Left};
};

class ExplanationView : public Biz::DataObserver<ExplanationPart>, public Yoga::Text
{
public:
    ExplanationView(size_t index, const ExplanationPart& data);

protected:
    void on_data_update() override;
};

class ExplanationContainer : public Yoga::Rectangle
{
public:
    explicit ExplanationContainer(Biz::ProjectInteractor& project_interactor);

    void update_explanation(const Biz::PrintToolItem& print_tool_item);

private:
    using ExplanationListView = Yoga::ListView<ExplanationView, ExplanationPart>;

    Biz::ProjectInteractor& m_project_interactor;

    Biz::UnsharedPointer<Biz::ObservableList<ExplanationPart>> m_explanation_list_labels;
    Biz::UnsharedPointer<Biz::ObservableList<ExplanationPart>> m_explanation_list;
    ExplanationListView* m_explanation_labels_list_view{nullptr};
    ExplanationListView* m_explanation_list_view{nullptr};
    Yoga::Text* m_explanation_label{nullptr};
    Yoga::Rectangle* m_formula_background{nullptr};
};

} // namespace Slic3r::App
