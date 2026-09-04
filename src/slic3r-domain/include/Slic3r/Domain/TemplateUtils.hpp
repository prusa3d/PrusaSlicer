#pragma once

#include <vector>
#include <optional>
#include <map>
#include <unordered_map>
#include <utility>
#include <variant>

namespace Slic3r::Domain {

template<typename T>
struct is_std_vector : std::false_type
{};

template<typename T, typename Alloc>
struct is_std_vector<std::vector<T, Alloc>> : std::true_type
{};

template<typename T>
inline constexpr bool is_std_vector_v = is_std_vector<T>::value;

template<typename T>
struct is_std_optional : std::false_type
{};

template<typename T>
struct is_std_optional<std::optional<T>> : std::true_type
{};

template<typename T>
inline constexpr bool is_std_optional_v = is_std_optional<T>::value;

template<typename T>
struct is_std_map : std::false_type
{};

template<typename Key, typename T, typename Compare, typename Allocator>
struct is_std_map<std::map<Key, T, Compare, Allocator>> : std::true_type
{};

template<typename Key, typename T, typename Hash, typename KeyEq, typename Allocator>
struct is_std_map<std::unordered_map<Key, T, Hash, KeyEq, Allocator>> : std::true_type
{};

template<typename T>
inline constexpr bool is_std_map_v = is_std_map<T>::value;

template<typename T, typename Variant>
struct is_in_variant;

template<typename T, typename... Ts>
struct is_in_variant<T, std::variant<Ts...>> : std::disjunction<std::is_same<T, Ts>...> {};

template<class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

// deduction guide
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;


// 1. Forward declare the helper struct
template<typename T>
struct deep_equality_check;

// 2. Define the Concept (It just wraps the struct's value)
template<typename T>
concept DeepEquality = deep_equality_check<T>::value;

// -----------------------------------------------------------------------------
// HELPER STRUCT SPECIALIZATIONS
// -----------------------------------------------------------------------------

// Base Case: Default to standard equality check
template<typename T>
struct deep_equality_check : std::bool_constant<std::equality_comparable<T>> {};

// Specialization for std::pair
// (Crucial for std::map, which is a range of pairs)
template<typename T, typename U>
struct deep_equality_check<std::pair<T, U>> {
    static constexpr bool value =
        std::equality_comparable<std::pair<T, U>> && // Check pair's own operator
        DeepEquality<T> &&                           // Recurse on Key
        DeepEquality<U>;                             // Recurse on Value
};

// Specialization for Ranges (Vectors, Lists, Arrays, Maps, Sets)
// We exclude std::string/string_view to stop recursion at characters
template<typename T>
concept IsRecursiveRange = std::ranges::input_range<T>
                           && !std::same_as<T, std::string>
                           && !std::same_as<T, std::string_view>;

template<IsRecursiveRange T>
struct deep_equality_check<T> {
    static constexpr bool value =
        std::equality_comparable<T> &&               // Check container's own operator
        DeepEquality<std::ranges::range_value_t<T>>; // Recurse on content
};

} // namespace Slic3r::Domain
