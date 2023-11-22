#ifndef UTILS_ENUMS_H_
#define UTILS_ENUMS_H_

#include <type_traits>

template <typename E, typename std::enable_if_t<std::is_enum_v<E>, bool> = true>
class as_flags : std::false_type {
};

template <typename E>
std::enable_if_t<as_flags<E>::value, E>
operator|(E lhs, E rhs) {
    using underlying = std::underlying_type_t<E>;
    return static_cast<E>(
            static_cast<underlying>(lhs) | static_cast<underlying>(rhs));
};

template <typename E>
std::enable_if_t<as_flags<E>::value, E>
operator&(E lhs, E rhs) {
    using underlying = std::underlying_type_t<E>;
    return static_cast<E>(
            static_cast<underlying>(lhs) & static_cast<underlying>(rhs));
};

template <typename E>
std::enable_if_t<as_flags<E>::value, E>
operator^(E lhs, E rhs) {
    using underlying = std::underlying_type_t<E>;
    return static_cast<E>(
            static_cast<underlying>(lhs) ^ static_cast<underlying>(rhs));
};

template <typename E>
std::enable_if_t<as_flags<E>::value, E>
operator~(E rhs) {
    using underlying = std::underlying_type_t<E>;
    return static_cast<E>(~static_cast<underlying>(rhs));
};

#endif  // UTILS_ENUMS_H_
