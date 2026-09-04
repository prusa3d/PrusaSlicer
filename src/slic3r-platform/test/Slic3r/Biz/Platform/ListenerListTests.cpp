#include <catch2/catch_test_macros.hpp>
#include "Slic3r/Biz/Platform/ListenerList.hpp"

TEST_CASE("Edit during invocation", "")
{
    class Interface {};
    Interface i1, i2;
    Slic3r::Biz::ListenerList<Interface> listeners;
    listeners.add(&i1);

    listeners.invoke([&](Interface* i) {
        listeners.remove(&i1); // remove itself
        listeners.add(&i2);
    });

    size_t count = 0;
    listeners.invoke([&](Interface* i) {
        CHECK(i == &i2);
        ++count;
    });
    CHECK(count == 1);
}

// Removed listener can't be called (even if removed in the invoke).
TEST_CASE("Deleted listener can't be called", "")
{
    struct Interface { virtual void check() {}; virtual ~Interface() = default; };
    class Deleted : public Interface { void check() override { CHECK(false); } };

    Interface i;
    Deleted d;

    Slic3r::Biz::ListenerList<Interface> listeners;
    listeners.add(&i);
    listeners.add(&d);
    listeners.invoke([&](Interface* i) {
        listeners.remove(&d);
        i->check();
    });
}

TEST_CASE("Added then removed listener can't be called", "")
{
    struct Interface
    {
        virtual void check() {};
        virtual ~Interface() = default;
    };

    class Added : public Interface
    {
        void check() override
        {
            CHECK(false);
        }
    };

    Interface i;
    Added a;

    Slic3r::Biz::ListenerList<Interface> listeners;
    listeners.add(&i);
    listeners.invoke(
        [&](Interface* listener)
        {
            listeners.add(&a);
            listeners.remove(&a);
            listener->check();
        }
    );

    size_t count = 0;
    listeners.invoke(
        [&](Interface* listener)
        {
            CHECK(listener == &i);
            ++count;
        }
    );
    CHECK(count == 1);
}

TEST_CASE("Duplicate add during invoke returns false", "")
{
    struct Interface
    {
        virtual ~Interface() = default;
    };

    Interface i;

    Slic3r::Biz::ListenerList<Interface> listeners;
    listeners.add(&i);

    listeners.invoke(
        [&](Interface*)
        {
            CHECK(listeners.add(&i) == false);
            CHECK(listeners.add(&i) == false);
        }
    );
}

TEST_CASE("Added listener is called in same invoke", "")
{
    struct Interface
    {
        virtual void check() {};
        virtual ~Interface() = default;
    };

    class Added : public Interface
    {
    public:
        explicit Added(size_t* calls)
            : m_calls{calls}
        {
        }

        void check() override
        {
            ++(*m_calls);
        }

    private:
        size_t* m_calls;
    };

    size_t calls = 0;
    Interface i;
    Added a{&calls};

    Slic3r::Biz::ListenerList<Interface> listeners;
    listeners.add(&i);

    listeners.invoke(
        [&](Interface* listener)
        {
            if (listener == &i) {
                listeners.add(&a);
            } else {
                listener->check();
            }
        }
    );

    CHECK(calls == 1);
}
