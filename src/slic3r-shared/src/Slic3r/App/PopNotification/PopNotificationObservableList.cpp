#include "Slic3r/App/PopNotification/PopNotificationObservableList.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"

namespace Slic3r::App::PopNotification {

void PopNotificationObservableList::upsert_notification(
    PopNotificationData data,
    PopNotificationObservableList::Matcher matcher
)
{
    auto it{std::find_if(
        m_notifications.begin(),
        m_notifications.end(),
        [&](const PopNotificationDataPtr& notif_ptr) { return data.type == notif_ptr->type && matcher(data.payload, notif_ptr->payload); }
    )};

    if (it == m_notifications.end()) {
        m_notifications.emplace_back(std::make_unique<PopNotificationData>(std::move(data)));
        it = std::prev(m_notifications.end());
        invoke_listeners<Biz::IListObserver<PopNotificationData>>(
            [this](auto* l)
            {
                const size_t index{m_notifications.size() - 1};
                l->on_inserted(this->at(index), index);
            }
        );
    } else {
        **it = data;
        const std::size_t index{
            static_cast<std::size_t>(std::distance(m_notifications.begin(), it))
        };
        invoke_listeners<Biz::IListObserver<PopNotificationData>>(
            [index](auto* l) { l->on_updated({index}); }
        );
        // To be removed when PopNotificationView items does call it correctly:
        Biz::Platform::PlatformServices::instance().render_request_handler().request_render();
    }
    reset_notification_timeout(it);
    // make sure the notification gets drawn
    Biz::Platform::PlatformServices::instance().render_request_handler().request_render();

}

template <typename T>
const T* PopNotificationObservableList::get_notification_payload(
    std::function<bool(const T&)> matcher
)
{
    const auto it{std::find_if(
        m_notifications.begin(),
        m_notifications.end(),
        [&](const PopNotificationDataPtr& notif_ptr) {
            auto payload{std::get_if<T>(&notif_ptr->payload)};
            if (payload == nullptr) {
                return false;
            }
            return matcher(*payload);
        }
    )};
    if (it == m_notifications.end()) {
        return nullptr;
    }
    return &std::get<T>((*it)->payload);
}
template const PrintHostProgressNotificationData* PopNotificationObservableList::get_notification_payload(
    std::function<bool(const PrintHostProgressNotificationData&)> matcher
);
template const UserAccountLoginNotificationData* PopNotificationObservableList::get_notification_payload(
    std::function<bool(const UserAccountLoginNotificationData&)> matcher
);

void PopNotificationObservableList::erase_notification(const PopNotificationData* to_erase)
{
    auto it{std::ranges::find_if(
        m_notifications,
        [to_erase](const PopNotificationDataPtr& notification)
        { return notification.get() == to_erase; }
    )};
    if (it != m_notifications.end()) {
        const auto index{std::distance(m_notifications.begin(), it)};
        erase_notification_by_index(index);
    }
}

void PopNotificationObservableList::erase_notification_by_index(size_t index)
{
    ASSERT(index < m_notifications.size());
    stop_notification_timer(m_notifications.begin() + index);
    m_notifications.erase(m_notifications.begin() + index);
    invoke_listeners<Biz::IListObserver<PopNotificationData>>(
        [&](auto* l) { l->on_removed({index}); }
    );
    // make sure the notification gets drawn
    Biz::Platform::PlatformServices::instance().render_request_handler().request_render();
}

void PopNotificationObservableList::erase_notification_by_predicate(
    std::function<bool(const PopNotificationData&)> predicate
)
{
    for (int i = m_notifications.size() - 1; i >= 0; --i) {
        if (predicate(*m_notifications[i])) {
            erase_notification_by_index((size_t) i);
        }
    }
}

void PopNotificationObservableList::notification_updated(size_t index)
{
}

void PopNotificationObservableList::reset_notification_timeout(PopNotificationDataIt it)
{
    PopNotificationData& notification{**it};
    const std::chrono::seconds timeout{notification.timeout};
    ASSERT(it != m_notifications.end());
    stop_notification_timer(it);

    if (timeout == 0s) {
        return;
    }

    using std::chrono::duration_cast;
    using std::chrono::milliseconds;
    auto& timer_queue{Biz::Platform::PlatformServices::instance().timer_queue()};
    notification.timer_id = timer_queue.set_timer(
        duration_cast<milliseconds>(timeout),
        std::bind(&PopNotificationObservableList::erase_notification, this, it->get()),
        false
    );
}

void PopNotificationObservableList::stop_notification_timer(PopNotificationDataIt it) const
{
    ASSERT(it != m_notifications.end());
    auto& timer_queue = Biz::Platform::PlatformServices::instance().timer_queue();
    const auto timer_id{it->get()->timer_id};
    if (timer_id != Biz::Platform::TimerQueue::TimerID()
        && timer_queue.is_timer_running(timer_id))
    {
        timer_queue.cancel_timer(timer_id);
    }
    it->get()->timeout = 0s;
}

void PopNotificationObservableList::on_notification_close_button(const PopNotificationData* to_close)
{
    auto it{std::ranges::find_if(
        m_notifications,
        [to_close](const PopNotificationDataPtr& notification)
        { return notification.get() == to_close; }
    )};
    if (it == m_notifications.end()) {
        return; 
    }
    const std::function<void()> on_close{(*it)->on_user_close};
    erase_notification_by_index((size_t) std::distance(m_notifications.begin(), it));
    if (on_close) {
        on_close();
    }
}

void PopNotificationObservableList::on_notification_hover(const PopNotificationData* hovered) {}

const PopNotificationData& PopNotificationObservableList::at(size_t index) const
{
    return *m_notifications[index].get();
}

size_t PopNotificationObservableList::size() const
{
    return m_notifications.size();
}

void PopNotificationObservableList::close_notifications_of_type(PopNotificationType type)
{
    for (int i = m_notifications.size() - 1; i >= 0; --i) {
        if (m_notifications[i]->type == type) {
            // m_notifications.erase(m_notifications.begin() + i);
            erase_notification_by_index((size_t) i);
        }
    }
}

} // namespace Slic3r::App::PopNotification
