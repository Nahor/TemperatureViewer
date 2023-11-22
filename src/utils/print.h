#ifndef PRINT_H_
#define PRINT_H_

#ifdef USE_FMT
#    include <fmt/core.h>
#    include <fmt/format.h>

#    include <fmt/chrono.h>
#else  // USE_FMT
#    include <format>
#endif  // !USE_FMT

// Convenient function to automatically add a new line after printing
// (in C++23, use std::println)

#ifdef USE_FMT
namespace fmt_ns = fmt;

inline void fmt_println(std::FILE *f, const char *str) {
    fmt::print(f, "{}\n", str);
}
inline void fmt_print(std::FILE *f, const char *str) {
    fmt::print(f, "{}", str);
}

template <typename... Args>
void fmt_println(std::FILE *f, fmt::format_string<Args...> fmt, Args &&...args) {
    std::string str = fmt::format(fmt, std::forward<Args>(args)...);
    fmt::print(f, "{}\n", str);
}
template <typename... Args>
void fmt_print(std::FILE *f, fmt::format_string<Args...> fmt, Args &&...args) {
    fmt::print(f, fmt, std::forward<Args>(args)...);
}
#else  // USE_FMT
namespace fmt_ns = std;

inline void fmt_println(std::FILE *f, const char *str) {
    fprintf(f, "%s\n", str);
}
inline void fmt_print(std::FILE *f, const char *str) {
    fprintf(f, "%s", str);
}

template <typename... Args>
inline void fmt_println(std::FILE *f, std::format_string<Args...> fmt, Args &&...args) {
    fprintf(f, "%s\n", std::format(std::format_string<Args...>(fmt), std::forward<Args>(args)...).c_str());
}
template <typename... Args>
inline void fmt_print(std::FILE *f, std::format_string<Args...> fmt, Args &&...args) {
    fprintf(f, "%s", std::format(std::format_string<Args...>(fmt), std::forward<Args>(args)...).c_str());
}

#endif  // !USE_FMT

inline void fmt_println(const char *str) {
    fmt_println(stdout, str);
}
inline void fmt_print(const char *str) {
    fmt_print(stdout, str);
}

template <typename... Args>
inline void fmt_println(fmt_ns::format_string<Args...> fmt, Args &&...args) {
    fmt_println(stdout, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void fmt_print(fmt_ns::format_string<Args...> fmt, Args &&...args) {
    fmt_print(stdout, fmt, std::forward<Args>(args)...);
}

#endif  // PRINT_H_
