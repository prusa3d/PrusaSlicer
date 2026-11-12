#pragma once

#include "Slic3r/App/PopNotification/PopNotificationData.hpp"
#include "Slic3r/Biz/IObservableList.hpp"

#include <vector>

namespace Slic3r::App::PopNotification {

using PopNotificationDataIt = std::vector<PopNotificationDataPtr>::iterator;

class PopNotificationObservableList : public Biz::IObservableList<PopNotificationData>
{
public:
    PopNotificationObservableList()  = default;
    ~PopNotificationObservableList() = default;

    PopNotificationObservableList(const PopNotificationObservableList&)            = delete;
    PopNotificationObservableList& operator=(const PopNotificationObservableList&) = delete;
    PopNotificationObservableList(PopNotificationObservableList&&)                 = delete;
    PopNotificationObservableList& operator=(PopNotificationObservableList&&)      = delete;

    using Matcher = std::function<bool(const PopNotificationPayload&, const PopNotificationPayload&)>;
    void upsert_notification(PopNotificationData data, Matcher matcher);

    template <typename T>
    const T* get_notification_payload(std::function<bool(const T&)> matcher);

    void close_notifications_of_type(PopNotificationType type);

    void on_notification_close_button(const PopNotificationData* to_close);
    void on_notification_hover(const PopNotificationData* hovered);

    const PopNotificationData& at(size_t index) const override;

    size_t size() const override;

    void erase_notification_by_predicate(std::function<bool(const PopNotificationData&)>);
    void erase_notification(const PopNotificationData* to_erase);

protected:
    void erase_notification_by_index(size_t index);
    void notification_updated(size_t index);
    void reset_notification_timeout(PopNotificationDataIt it);
    void stop_notification_timer(PopNotificationDataIt it) const;

    std::vector<PopNotificationDataPtr> m_notifications;
};

inline const PopNotificationObservableList::Matcher never_equal_matcher = 
    [](const PopNotificationPayload&, const PopNotificationPayload&) {
    return false;
};

inline const PopNotificationObservableList::Matcher always_equal_matcher = 
    [](const PopNotificationPayload&, const PopNotificationPayload&) {
    return true;
};

} // namespace Slic3r::App::PopNotification
