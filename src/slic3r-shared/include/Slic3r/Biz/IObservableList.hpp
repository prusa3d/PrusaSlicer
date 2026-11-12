#pragma once

#include "Slic3r/Biz/IListObserver.hpp"

#include "Slic3r/Biz/Platform/WithListeners.hpp"

namespace Slic3r::Biz {

template <class Data>
class IObservableList : public WithListeners<IListObserver<Data>>
{
public:
    virtual ~IObservableList() = default;

    /**
     * @return const reference to element at index
     */
    virtual const Data& at(size_t index) const = 0;

    /**
     * @return size of items in list
     */
    virtual size_t size() const = 0;
};

/**
 * @brief Concept of any `IObservableList<E>` implementation
 */
template <typename T, typename E>
concept ObservableListType = std::is_base_of_v<IObservableList<E>, T>;

} // namespace Slic3r::Biz
