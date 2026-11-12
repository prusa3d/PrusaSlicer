#pragma once

#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

namespace Slic3r::App {

class MaterialSelectionRow :
    public Biz::DataObserver<Biz::Preset::PresetItem>,
    public Yoga::LayoutButton
{
public:
    using FnClicked = std::function<void()>;
    using FnIndexClicked = std::function<void(size_t)>;
    using FnChecked = std::function<bool(size_t)>;

    explicit MaterialSelectionRow(
        size_t index,
        const Biz::Preset::PresetItem& data,
        FnClicked on_clicked_extention,
        FnIndexClicked on_favorite_clicked,
        FnChecked is_favorite_checked,
        FnClicked on_cog_clicked,
        size_t& material_index,
        Biz::Preset::PresetInteractor& preset_interactor
    );

protected:
    void on_data_update() override;
    void checked_updated_internal() override;
    void hovered_updated_internal() override;
    void update_buttons_visibility();

private:
    size_t& m_material_index;
    FnClicked m_on_clicked_extention;
    FnIndexClicked m_on_favorite_clicked;
    FnChecked m_is_favorite_checked;
    FnClicked m_on_cog_clicked;
    Biz::Preset::PresetInteractor& m_preset_interactor;
    LayoutButton* m_favorite_btn{nullptr};
    LayoutButton* m_cog_btn{nullptr};
};

} // namespace Slic3r::App
