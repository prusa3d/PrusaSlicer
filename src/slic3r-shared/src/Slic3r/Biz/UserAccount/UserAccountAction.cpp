#include "Slic3r/Biz/UserAccount/UserAccountAction.hpp"

#include "Slic3r/Biz/Network/IHttp.hpp"
#include "Slic3r/Log.hpp"

#include "fmt/format.h"

namespace Slic3r::Biz::UserAccount {

void UserAccountActionPost::perform(
    IUserAccountActionCallbacks* callbacks,
    /*UNUSED*/ const std::string& access_token,
    ActionQueueData&& action_data,
    std::atomic_bool& global_cancel
) const
{
    auto shared_data = std::make_shared<ActionQueueData>(std::move(action_data));
    std::string url = m_url;
    // SPDLOG_INFO("{}: {}", __FUNCTION__, url);

    auto retry_fn = [&global_cancel, &callbacks, action_name = &m_action_name](Network::IHttp::Retry retry, bool& cancel)
    {
        cancel = global_cancel;
        if (cancel) {
            return;
        }
        SPDLOG_INFO("Action POST retry attempt {} ({}): {} ms to next attempt.", retry.attempt, *action_name, retry.ms_to_next_attempt);
        if (retry.attempt > 1 && retry.just_tried) {
            callbacks->on_action_retry(retry);
        }
    };

    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(Network::IHttp::RequestMethod::Post, std::move(url), retry_fn);
    if (!shared_data->input.empty())
        http->set_post_body(shared_data->input);

    bool content_type_added = false;
    if (!shared_data->additional_headers.empty()) {
        for (const auto& [key, value] : shared_data->additional_headers) {
            bool is_content_type = std::ranges::equal(
                key,
                "Content-Type",
                [](char a, char b)
                {
                    return std::tolower(static_cast<unsigned char>(a))
                        == std::tolower(static_cast<unsigned char>(b));
                }
            );

            if (is_content_type) {
                content_type_added = true;
            }
            http->header(key, value);
        }
    }
    if (!content_type_added) {
        http->header("Content-Type", "application/x-www-form-urlencoded");
    }

    http->on_progress(
            [shared_data, &global_cancel](Network::IHttp::Progress progress, bool& cancel)
            {
                const bool cancel_notification = cancel;
                if (global_cancel) {
                    cancel = true;
                }
                if (!cancel_notification) {
                    return;
                }
                if (shared_data->fail_callback) {
                    shared_data->fail_callback({});
                }
            }
        )
        .on_error(
            [shared_data](std::string body, std::string error, unsigned status)
            {
                // SPDLOG_INFO("UserActionPost::perform on_error");
                if (shared_data->fail_callback)
                    shared_data->fail_callback(body);
            }
        )
        .on_complete(
            [shared_data](std::string body, unsigned status)
            {
                // SPDLOG_INFO("UserActionPost::perform on_complete");
                if (shared_data->success_callback)
                    shared_data->success_callback(body);
            }
        )
        .perform_sync(Network::HttpRetryOpt::default_retry());
}

void UserAccountActionGetWithEvent::perform(
    IUserAccountActionCallbacks* callbacks,
    const std::string& access_token,
    ActionQueueData&& action_data,
    std::atomic_bool& global_cancel
) const
{
    auto shared_data = std::make_shared<ActionQueueData>(std::move(action_data));
    std::string url = m_url;
    std::string post_body;
    if (url.empty()) {
        url = shared_data->input;
    } else {
        post_body = shared_data->input;
    }

    // SPDLOG_INFO("{}: {}", __FUNCTION__, url);

    auto retry_fn = [&global_cancel, &callbacks, action_name = &m_action_name](Network::IHttp::Retry retry, bool& cancel)
    {
        cancel = global_cancel;
        if (cancel) {
            return;
        }
        SPDLOG_INFO("Action GET retry attempt {} ({}): {} ms to next attempt.", retry.attempt, *action_name, retry.ms_to_next_attempt);
        if (retry.attempt > 1 && retry.just_tried) {
            callbacks->on_action_retry(retry);
        }
    };

    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(Network::IHttp::RequestMethod::Get, std::move(url), retry_fn);
    if (!post_body.empty())
        http->set_post_body(post_body);
    if (!access_token.empty()) {
        http->header("Authorization", "Bearer " + access_token);
#ifndef NDEBUG
        // In debug mode, also verify the token expiration
        // This is here to help with "dev" accounts with shorten (sort of faked) expiration time
        // The /api/v1/me will accept these tokens even if these are fake-marked as expired
        /*
        if (!Utils::verify_exp(access_token) && fail_callback) {
            fail_callback("Token Expired");
        }
        */
#endif
    }
    if (!shared_data->additional_headers.empty()) {
        for (const auto& [key, value] : shared_data->additional_headers) {
            http->header(key, value);
        }
    }
    http->on_error(
            [shared_data, action_name = &m_action_name, &callbacks, fail_type = m_fail_type](std::string body, std::string error, unsigned status)
            {
                // SPDLOG_INFO("UserActionGetWithEvent::perform on_error");
                if (shared_data->fail_callback)
                    shared_data->fail_callback(body);
                std::string message = fmt::format("{} action failed ({}): {}", *action_name, std::to_string(status), body);
                if (fail_type != ActionFailType::None) {
                    callbacks->on_action_fail(fail_type, std::move(message));
                }
            }
    )
        .on_progress(
            [shared_data, action_name = &m_action_name, &callbacks, fail_type = m_fail_type, &global_cancel](Network::IHttp::Progress progress, bool& cancel)
            {
                const bool cancel_notification = cancel;
                if (global_cancel) {
                    cancel = true;
                }
                if (!cancel_notification) {
                    return;
                }
                if (shared_data->fail_callback) {
                    shared_data->fail_callback({});
                }
                std::string message = fmt::format("{} action canceled", *action_name);
                if (fail_type != ActionFailType::None) {
                    callbacks->on_action_fail(fail_type, std::move(message));
                }
            }
        )
        .on_complete(
            [shared_data, &callbacks, success_type = m_success_type](std::string body, unsigned status)
            {
                // SPDLOG_INFO("UserActionGetWithEvent::perform on_complete");
                if (shared_data->success_callback)
                    shared_data->success_callback(body);
                if (success_type != ActionSuccessType::None) {
                    callbacks->on_action_success(success_type, std::move(body));
                }
            }
        )
        .perform_sync(Network::HttpRetryOpt::default_retry());
}

} // namespace Slic3r::Biz::UserAccount
