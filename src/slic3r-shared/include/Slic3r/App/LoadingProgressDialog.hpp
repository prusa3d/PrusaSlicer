#pragma once

#include "Slic3r/App/LoadingProgressController.hpp"
#include "Slic3r/App/Yoga/Dialog.hpp"

namespace Slic3r::App {

namespace Yoga {
class Text;
class ProgressBar;
} // namespace Yoga

/**
 * @brief Modal dialog ModalDialog::LoadingProgress, shows the progress of the job tracked by
 * LoadingProgressController.
 */
class LoadingProgressDialog : public Yoga::Dialog, public ILoadingProgressChangedListener
{
public:
    explicit LoadingProgressDialog(LoadingProgressController& controller);
    ~LoadingProgressDialog() override;

    void on_loading_progress_changed(const std::string& label, int percent) override;

private:
    LoadingProgressController& m_controller;

    Yoga::Text* m_label{nullptr};
    Yoga::ProgressBar* m_progress_bar{nullptr};
};

} // namespace Slic3r::App
