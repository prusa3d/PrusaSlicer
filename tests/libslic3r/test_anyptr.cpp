#include <catch2/catch_test_macros.hpp>

#include <libslic3r/libslic3r.h>
#include <libslic3r/AnyPtr.hpp>
#include <test_utils.hpp>

class Foo
{
public:
    virtual ~Foo() = default;

    virtual void set_foo(int) = 0;
    virtual int  get_foo() const =  0;
};

class Bar: public Foo
{
    int m_i = 0;

public:
    virtual void set_foo(int i) { m_i = i; }
    virtual int  get_foo() const { return m_i; };
};

class BarPlus: public Foo {
    int m_i = 0;

public:
    virtual void set_foo(int i) { m_i = i + 1; }
    virtual int  get_foo() const { return m_i; };
};

TEST_CASE("Testing AnyPtr", "[anyptr]") {
    using Slic3r::AnyPtr;

    SECTION("Construction with various valid arguments using operator=")
    {
        auto args = std::make_tuple(nullptr,
                                    AnyPtr<Foo>{nullptr},
                                    AnyPtr{static_cast<Foo *>(nullptr)},
                                    AnyPtr{static_cast<Bar *>(nullptr)},
                                    AnyPtr{static_cast<BarPlus *>(nullptr)},
                                    AnyPtr<Foo>{},
                                    AnyPtr<Bar>{},
                                    AnyPtr<BarPlus>{},
                                    static_cast<Foo *>(nullptr),
                                    static_cast<Bar *>(nullptr),
                                    static_cast<BarPlus *>(nullptr));

        auto check_ptr = [](auto &ptr) {
            REQUIRE(!ptr);
            REQUIRE(!ptr.is_owned());

            auto shp = ptr.get_shared_cpy();
            REQUIRE(!shp);
        };

        SECTION("operator =") {
            Slic3r::for_each_in_tuple([&check_ptr](auto &arg){
                AnyPtr<const Foo> ptr = std::move(arg);

                check_ptr(ptr);
            }, args);
        }

        SECTION("move construction")
        {
            Slic3r::for_each_in_tuple([&check_ptr](auto &arg){
                AnyPtr<const Foo> ptr{std::move(arg)};

                check_ptr(ptr);
            }, args);
        }
    }

    GIVEN("A polymorphic base class type Foo") {
        WHEN("Creating a subclass on the stack") {
            Bar bar;
            auto val = random_value(-100, 100);
            bar.set_foo(val);

            THEN("Storing a raw pointer in an AnyPtr<Foo> should be valid "
                 "until the object is not destroyed")
            {
                AnyPtr<Foo> ptr = &bar;
                auto val2 = random_value(-100, 100);
                ptr->set_foo(val2);

                REQUIRE(ptr->get_foo() == val2);
            }

            THEN("Storing a raw pointer in an AnyPtr<const Foo> should be "
                 "valid until the object is not destroyed")
            {
                AnyPtr<const Foo> ptr{&bar};

                REQUIRE(ptr->get_foo() == val);
            }
        }
    }

    GIVEN("An empty AnyPtr of type Foo")
    {
        AnyPtr<Foo> ptr;

        WHEN("Re-assigning a new unique_ptr of object of type Bar to ptr")
        {
            auto bar = std::make_unique<Bar>();
            auto val = random_value(-100, 100);

            bar->set_foo(val);

            ptr = std::move(bar);

            THEN("the ptr should contain the new object and should own it")
            {
                REQUIRE(ptr->get_foo() == val);
                REQUIRE(ptr.is_owned());
            }
        }

        WHEN("Re-assigning a new unique_ptr of object of type BarPlus to ptr")
        {
            auto barplus = std::make_unique<BarPlus>();
            auto val = random_value(-100, 100);

            barplus->set_foo(val);

            ptr = std::move(barplus);

            THEN("the ptr should contain the new object and should own it")
            {
                REQUIRE(ptr->get_foo() == val + 1);
                REQUIRE(ptr.is_owned());
            }

            THEN("copying the stored object into a shared_ptr should be invalid")
            {
                std::shared_ptr<Foo> shptr = ptr.get_shared_cpy();

                REQUIRE(!shptr);
            }

            THEN("copying the stored object into a shared_ptr after calling "
                 "convert_unique_to_shared should be valid")
            {
                ptr.convert_unique_to_shared();
                std::shared_ptr<Foo> shptr = ptr.get_shared_cpy();

                REQUIRE(shptr);
                REQUIRE(shptr->get_foo() == val + 1);
            }
        }
    }

    GIVEN("A vector of AnyPtr<Foo> pointer to random Bar or BarPlus objects")
    {
        std::vector<AnyPtr<Foo>> ptrs;

        auto N = random_value(size_t(1), size_t(10));
        INFO("N = " << N);

        std::generate_n(std::back_inserter(ptrs), N, []{
            auto v = random_value(0, 1);

            std::unique_ptr<Foo> ret;

            if (v)
                ret = std::make_unique<Bar>();
            else
                ret = std::make_unique<BarPlus>();

            return ret;
        });

        WHEN("moving the whole array into a vector of AnyPtr<const Foo>")
        {
            THEN("the move should be valid")
            {
                std::vector<AnyPtr<const Foo>> constptrs;
                std::vector<int> vals;
                std::transform(ptrs.begin(), ptrs.end(),
                               std::back_inserter(vals),
                               [](auto &p) { return p->get_foo(); });

                std::move(ptrs.begin(), ptrs.end(), std::back_inserter(constptrs));

                REQUIRE(constptrs.size() == N);
                REQUIRE(ptrs.size() == N);
                REQUIRE(std::all_of(ptrs.begin(), ptrs.end(), [](auto &p) { return !p; }));

                for (size_t i = 0; i < N; ++i) {
                    REQUIRE(vals[i] == constptrs[i]->get_foo());
                }
            }
        }
    }
}
