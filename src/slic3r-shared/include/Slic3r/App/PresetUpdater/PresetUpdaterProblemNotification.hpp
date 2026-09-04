#pragma once

#include "Slic3r/Biz/PresetUpdater/PresetUpdaterReason.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::App {

/// Carries everything a check or an install ran into, and any job that failed outright.
class PresetUpdaterProblemNotification
{
public:
    struct Problem
    {
        Biz::PresetUpdater::PresetUpdaterReason reason;
        /// Display name of the source, or its id while no row knows it by name.
        std::string source_name;
        /// Empty when the whole source is affected rather than one of its vendors.
        std::string vendor_id;

        bool operator==(const Problem& other) const = default;
    };

    /// Replaces what the last check or install reported. An empty list takes it down.
    void report_problems(std::vector<Problem> problems);

    /// @param subject what the job was about, or empty when its failure has nothing to name.
    void report_error(Biz::PresetUpdater::PresetUpdaterReason reason, std::string subject);

    /// Drops the problems and the error, for a check that is about to answer them afresh.
    void rearm();

    void set_dialog_open(bool dialog_open);

    void set_show_dialog_callback(std::function<void()> callback);

    void refresh();

    /// Drops what was collected. For teardown only, so it never touches the notification center.
    void reset();

private:
    void refresh_problems();
    void refresh_error();

    std::vector<Problem> m_problems;
    std::optional<Biz::PresetUpdater::PresetUpdaterReason> m_error;
    std::string m_error_subject;
    /// Zero until the user closes the dialog, so what is on screen stays while they are in it.
    std::chrono::seconds m_timeout{0};
    bool m_dialog_open{false};
    std::function<void()> m_show_dialog_callback;
};

} // namespace Slic3r::App
