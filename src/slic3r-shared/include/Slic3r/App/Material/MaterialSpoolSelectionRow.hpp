#pragma once

#include "Slic3r/Biz/MatDb/MatDbInteractor.hpp"
#include "Slic3r/Biz/DataObserver.hpp"

#include "Slic3r/App/Yoga/RectangleButton.hpp"

namespace Slic3r::App::Yoga {
class Icon;
class Text;
class ComboBox;
class LayoutButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class MaterialSpoolSelectionRow :
    public Biz::DataObserver<Biz::MatDb::MatDbItem>,
    public Yoga::RectangleButton
{
public:
    using CheckedFn = std::function<void(size_t index)>;
    using CloseFn   = std::function<void()>;

    explicit MaterialSpoolSelectionRow(
        size_t index,
        const Biz::MatDb::MatDbItem& data,
        Biz::MatDb::MatDbInteractor& matdb_interactor,
        const size_t& material_index,
        CheckedFn checked_fn,
        CloseFn close_fn
    );

protected:
    void on_data_update() override;
    void checked_updated_internal() override;
    void action_internal() override;

private:
    void update_generic_warning();

private:
    Biz::MatDb::MatDbInteractor& m_matdb_interactor;
    const size_t& m_material_index;
    CheckedFn m_checked_fn{nullptr};
    CloseFn m_close_fn{nullptr};

    Yoga::Text* m_spool_name{nullptr};
    Yoga::Icon* m_spool_icon{nullptr};
    Yoga::Icon* m_favorite_icon{nullptr};
    Yoga::ComboBox* m_preset_combobox{nullptr};
    Yoga::Item* m_bottom_row{nullptr};
    Yoga::LayoutButton* m_confirm_button{nullptr};
    Yoga::Text* m_generic_warning{nullptr};
};

} // namespace Slic3r::App
