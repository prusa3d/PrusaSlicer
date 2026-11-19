#include "Slic3r/App/LoadingProgressDialog.hpp"

#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/ProgressBar.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"

namespace Slic3r::App {

LoadingProgressDialog::LoadingProgressDialog(LoadingProgressController& controller) :
    // TRN Loading progress dialog title.
    Dialog({Biz::_u8L("Loading")}, "LoadingProgressDialog"),
    m_controller(controller)
{
    using namespace Yoga;

    set_closable(false);

    content_item()->set_modal(true);
    content_item()->set_width(400_fpx);

    content()->set_orientation(Orientation::Vertical);
    content()->set_gap(10_fpx);

    m_label = content()->emplace_back<Text>(m_controller.label());
    m_label->set_wrap_mode(Text::WrapMode::Wrap);

    m_progress_bar = content()->emplace_back<ProgressBar>();
    m_progress_bar->set_height(20_fpx);
    m_progress_bar->set_progress(m_controller.percent());

    Item* buttons_row = content()->emplace_back<Item>();
    buttons_row->set_orientation(Orientation::Horizontal);
    buttons_row->set_justify_content(YGJustifyFlexEnd);

    LayoutButton* cancel_button = buttons_row->emplace_back<LayoutButton>(Biz::_u8L("Cancel"));
    cancel_button->set_content_padding(Paddings(15_fpx, 5_fpx));
    cancel_button->callbacks().action = [this]() { m_controller.cancel(); };

    m_controller.add_listener<ILoadingProgressChangedListener>(this);
}

LoadingProgressDialog::~LoadingProgressDialog()
{
    m_controller.remove_listener<ILoadingProgressChangedListener>(this);
}

void LoadingProgressDialog::on_loading_progress_changed(const std::string& label, int percent)
{
    m_label->set_text(label);
    m_progress_bar->set_progress(percent);
    if (opened()) {
        Biz::Platform::PlatformServices::instance().render_request_handler().request_render();
    }
}

} // namespace Slic3r::App
