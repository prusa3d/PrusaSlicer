#pragma once

#include <functional>
#include <vector>
#include <memory>
#include "Slic3r/Assert.hpp"

namespace Slic3r::Biz::Platform {
class MoveOnlyFunction {
private:
    struct Base {
        virtual void invoke() = 0;
        virtual ~Base() = default;
    };

    template<typename Callable>
    struct Model : public Base {
        Model(Callable&& callable): callable{std::forward<Callable>(callable)} {}
        Model(const Callable& callable): callable{callable} {}

        void invoke() override {
            return callable();
        };

        Callable callable;
    };

    std::unique_ptr<Base> callable;

public:
    template<typename Callable>
    MoveOnlyFunction(Callable&& callable)
        : callable{std::make_unique<Model<std::decay_t<Callable>>>(std::forward<Callable>(callable))} {}

    template<typename Callable>
    MoveOnlyFunction(const Callable& callable)
        : callable{std::make_unique<Model<std::decay_t<Callable>>>(callable)} {}

    void operator()() {
        callable->invoke();
    }
};
}
