#pragma once

#include "Slic3r/App/Yoga/RectangleButton.hpp"
#include "Slic3r/Biz/DataObserver.hpp"

#include <functional>

namespace Slic3r::App::Yoga {
class LambdaItem;
class Rectangle;
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::ColorMix {

struct ExtruderFilterData
{
    unsigned int extruder_id_1based{1};
    ImColor color{0x80, 0x80, 0x80};
    bool enabled{true};
};

/**
 * @brief Toggle button of one physical extruder that filters which blends are offered.
 */
class ExtruderFilterButton :
    public Yoga::RectangleButton,
    public Biz::DataObserver<ExtruderFilterData>
{
public:
    using FnIndexClicked = std::function<void(size_t)>;

    ExtruderFilterButton(size_t index, const ExtruderFilterData& data, FnIndexClicked on_clicked);

protected:
    void on_data_update() override;

private:
    Yoga::Rectangle* m_swatch{nullptr};
    Yoga::Text* m_label{nullptr};
    Yoga::LambdaItem* m_cross{nullptr};

    FnIndexClicked m_on_clicked;
};

} // namespace Slic3r::App::ColorMix
