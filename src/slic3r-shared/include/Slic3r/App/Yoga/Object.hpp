#pragma once

#include "Slic3r/App/Yoga/Namespace.hpp"
#include "Slic3r/App/Theme.hpp"

#include "Slic3r/Biz/IListObserver.hpp"

namespace Slic3r::App::Yoga {

class Event;

class Object;
using ObjectPtr = std::unique_ptr<Object>;

using ObjectHeartBeat = std::weak_ptr<int>;

/**
 * @brief The Object class is an Object in a tree
 */
class Object
{
public:
    Object();
    virtual ~Object();
    Object(const Object& rhs)      = delete;
    Object& operator=(Object& rhs) = delete;

    const std::string& object_name() const;
    void set_object_name(const std::string& object_name);

    virtual void resize(const SizeInfo& size_info);

    /**
     * @note Layout style cannot be changed inside render
     * @note You have to call render() of your children as well
     */
    virtual void render(const Vec2f& pos, const Vec2f& size);

    /**
     * @brief Layout aware & dynamic code should be put here
     * @note You have to call style_node() of your children as well
     */
    virtual void style_node();

    /**
     * @warning will request scene redraw
     */
    virtual void set_style_dirty();

    Object* parent() const;

    template <class T, class... Args>
    T* emplace_back(Args&&... args)
    {
        std::unique_ptr<T> item = std::make_unique<T>(std::forward<Args>(args)...);
        T* item_raw             = item.get();
        append(std::move(item));
        return item_raw;
    }

    template <class T, class... Args>
    T* emplace(size_t index, Args&&... args)
    {
        std::unique_ptr<T> item = std::make_unique<T>(std::forward<Args>(args)...);
        T* item_raw             = item.get();
        insert(std::move(item), index);
        return item_raw;
    }

    std::vector<Object*> objects() const;

    virtual void prepend(ObjectPtr child);
    virtual void append(ObjectPtr child);
    virtual void insert(ObjectPtr child, size_t index);

    /**
     * @warning Immediate remove Item from tree, for deffered use remove_later
     * @param child to remove
     */
    virtual ObjectPtr remove(Object* child);
    /**
     * @brief remove_later removes item only after render traversal
     * @param child to remove
     */
    void remove_later(Object* child);
    /**
     * @brief move_later moves this item only after render traversal
     * @param target to move this item
     * @param index to move this item
     */
    void move_later(Object* target, size_t index);
    size_t object_count() const;
    Object* get_object(size_t index) const;
    std::optional<size_t> index_of(Object* item) const;

    virtual void push_event(std::unique_ptr<Event> event);

    /**
     * @warning ignore this and do not use
     */
    ObjectHeartBeat heartbeat() const;

    static void set_theme(Theme* theme);

    static void set_scale_factor(float scale_factor);
    static float scale_factor();
    static float pixel_round(float value);
    static Vec2f pixel_round(const Vec2f& value);
    static ImVec2 pixel_round(const ImVec2& value);

private:
    /**
     * @note intentionally private, please use append/prepend/insert
     */
    void add_child(ObjectPtr child, size_t index);
    /**
     * @note intentionally private, please use remove/remove_later
     */
    ObjectPtr remove_child(Object* child);

protected:
    Object* root_item() const;

    SizeInfo m_size_info;

    virtual void root_item_about_to_update();
    virtual void root_item_updated();

protected:
    /**
     * @note Stored here only for convenience, it is assumed that Derived classes
     * do not modify children directly
     * @todo move to method
     */
    std::vector<ObjectPtr> m_children;

    static Theme* m_theme;
    static YGConfigRef m_config;

private:
    static std::unordered_map<std::string, int> m_object_names;

    Biz::UnsharedPointer<int> m_heartbeat;

    Object* m_parent = nullptr;
    Object* m_root   = this;

    std::string m_object_name;
};

template <class T>
std::unique_ptr<T> unique_dynamic_cast(ObjectPtr item)
{
    return std::unique_ptr<T>(dynamic_cast<T*>(item.release()));
}

}