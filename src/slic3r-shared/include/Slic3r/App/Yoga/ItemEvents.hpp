#pragma once

#include "Slic3r/App/Yoga/Item.hpp"

#include <memory>
#include <list>

namespace Slic3r::App::Yoga {

class RootItem;

class Event
{
public:
    struct Change
    {
        enum class AffectedResult
        {
            Removed,
            Inserted,
        };
        AffectedResult affected_result{AffectedResult::Removed};
        Object* parent{nullptr};
        size_t new_index{0};
    };

    using ChangeList = std::list<Change>;

    Event(Object* item);
    virtual ~Event();

    virtual ChangeList process() = 0;

    virtual void affected(const ChangeList& change_list);

    virtual bool is_valid() const;

protected:
    Object* m_item = nullptr;
    ObjectHeartBeat m_object_heartbeat;
    ObjectHeartBeat m_parent_heartbeat;
};

using EventPtr = std::unique_ptr<Event>;

class RemoveEvent : public Event
{
public:
    RemoveEvent(Item* item);

    ChangeList process() override;
};

class MoveEvent : public Event
{
public:
    MoveEvent(Object* item, Object* new_parent, size_t new_index);

    ChangeList process() override;

    void affected(const ChangeList& change_list) override;

private:
    Object* m_new_parent = nullptr;
    ObjectHeartBeat m_new_parent_heartbeat;
    size_t m_new_index = 0;
};

/**
 * @brief The LoopEvents class - Stores and processes Events
 * Each event can produce ChangeList which then can affect other
 * events in loop
 */
class LoopEvents
{
public:
    explicit LoopEvents(RootItem& root_item);

    void insert_event(EventPtr event);

    /**
     * @return number of processed events. if > 0, something in the tree did change
     */
    unsigned int process_events();

private:
    RootItem& m_root_item;
    std::list<EventPtr> m_events;
};

} // namespace Slic3r::App::Yoga
