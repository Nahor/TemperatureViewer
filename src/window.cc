// spell-checker:words APIENTRY bugprone datapoint datapoints dockspace FRAMEBUFFER fullscreen gamepad Nahor
// spell-checker:words NOLINTNEXTLINE Nsight OPENGL plotpoint timepoint
// spell-checker:words mkgmtime
//
// spell-checker:words glfw GLFWvidmode GLsizei
// spell-checker:words glGetFloatv glGetIntegeri_v glGetIntegerv
// spell-checker:words glGetNamedFramebufferAttachmentParameteriv
#include "window.h"

#include <GLFW/glfw3.h>
#include <glad/gl.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <glm/glm.hpp>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>

#include "engine/tracy.h"
#include "ui/imgui_env.h"
#include "utils/print.h"

#if defined(__clang__)
// warning: bitwise operation between different enumeration types ('XXXFlags_' and 'XXXFlagsPrivate_') is deprecated
#    pragma clang diagnostic ignored "-Wdeprecated-enum-enum-conversion"
#endif  // __clang__

#define APP_GL_DEBUG

static constexpr const char* kSensorCsvFile = R"(Sensor.csv)";

static constexpr const char* kTracyFrame = "Frame";
static constexpr const char* kTracyZoneImgui = "ImGui";
static constexpr const char* kTracyZoneGameRender = "GameRender";
static constexpr const char* kTracyZoneInput = "Input";
static constexpr const char* kTracyPollEvents = "PollEvents";

static constexpr const char* kTitle = "Temperature Viewer";
static constexpr int kDefaultWindowWidth = 1920;

static constexpr int kModMask = GLFW_MOD_SHIFT | GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER;

static constexpr int sec_per_min = 60;
static constexpr int min_per_hour = 60;
static constexpr int hour_per_day = 24;
static constexpr int month_per_year = 12;
static constexpr double day_per_year = 365.24;
static constexpr double day_per_month = day_per_year / month_per_year;

static constexpr int sec_per_hour = sec_per_min * min_per_hour;
static constexpr int sec_per_day = sec_per_hour * hour_per_day;
static constexpr int sec_per_month = static_cast<int>(sec_per_day * day_per_month);  // note: with the chosen day_per_year, this is still an int
static constexpr int sec_per_year = sec_per_month * month_per_year;

static constexpr int min_per_day = sec_per_day / sec_per_min;

enum class Degree {
    Unknown,
    Celsius,
    Fahrenheit
};

void APIENTRY glDebugCallback(GLenum source, GLenum type, unsigned int id,
        GLenum severity, GLsizei length, const char* message, const void* userParam);

constexpr auto operator<=>(const ImPlotRange& lfs, const ImPlotRange& rhs) {
    auto r = (lfs.Min <=> rhs.Min);
    if (r == std::partial_ordering::equivalent) {
        r = (lfs.Max <=> rhs.Max);
    }
    return r;
}

constexpr auto operator<=>(const ImPlotRect& lfs, const ImPlotRect& rhs) {
    auto r = (lfs.X <=> rhs.X);
    if (r == std::partial_ordering::equivalent) {
        r = (lfs.Y <=> rhs.Y);
    }
    return r;
}

constexpr auto operator<=>(const ImVec2& lfs, const ImVec2& rhs) {
    auto r = (lfs.x <=> rhs.x);
    if (r == std::partial_ordering::equivalent) {
        r = (lfs.y <=> rhs.y);
    }
    return r;
}

namespace {

time_t TmToTime(struct tm& tm) {
    return ImPlot::GetStyle().UseLocalTime ? mktime(&tm) : _mkgmtime(&tm);
}

errno_t TimeToTm(time_t time, struct tm& tm) {
    return ImPlot::GetStyle().UseLocalTime ? localtime_s(&tm, &time) : gmtime_s(&tm, &time);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
time_t shiftDate(time_t time, int target_year) {
    if (target_year == 0) {
        return time;
    }

    tm tm{};
    TimeToTm(time, tm);

    tm.tm_year = target_year - 1900;
    // TODO(nahor): assumes daylight saving will always be based on the tm_isdst field rather than the timezone dates

    return TmToTime(tm);
}

time_t getSysTime(struct tm& tm, time_t prev_time) {
    struct tm tm_std = tm;
    tm_std.tm_isdst = 0;
    struct tm tm_dst = tm;
    tm_dst.tm_isdst = 1;

    time_t time_std = std::mktime(&tm_std);
    time_t time_dst = std::mktime(&tm_dst);
    time_t time{};
    if (tm_std.tm_isdst == tm_dst.tm_isdst) {
        // Mktime knows for sure what the daylight saving is, so use the correct one
        time = (tm_std.tm_isdst == 0) ? time_std : time_dst;
    } else if (prev_time == 0) {  // NOLINT(bugprone-branch-clone)
        // Tough luck, we start with date where dst cannot be automatically determined
        // => use DST, worst case: it wasn't DST and we'll have a gap in the datetime
        // If instead we were to use STD and it was actually DST, we would have overlapping
        // datetime instead, which is more problematic (e.g. the Data is no longer sorted by time)
        time = time_dst;
    } else if (std::abs(time_dst - prev_time) <= 65) {
        time = time_dst;
    } else if (std::abs(time_std - prev_time) <= 65) {
        time = time_std;
    } else {
        fmt_println("Can't figure out which time to use for {:04}-{:02}-{:02}_{:02}:{:02} => dst {} vs std {}",
                tm.tm_year, tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min,
                (time_dst - prev_time), (time_std - prev_time));
        time = time_dst;
    }
    return time;
}

template <typename Clock, typename Dur>
std::string getDateTimeStr(std::chrono::time_point<Clock, Dur> timepoint, bool with_seconds = false) {
    std::string datetime_str;
    if (ImPlot::GetStyle().UseISO8601) {
        datetime_str = fmt_ns::format("{:%Y-%m-%d}", timepoint);
    } else {
        datetime_str = fmt_ns::format("{:%b/%d/%y}", timepoint);
    }
    if (ImPlot::GetStyle().Use24HourClock) {
        if (with_seconds) {
            datetime_str += fmt_ns::format(" {:%H:%M:%S}", timepoint);
        } else {
            datetime_str += fmt_ns::format(" {:%H:%M}", timepoint);
        }
    } else if (with_seconds) {
        datetime_str += fmt_ns::format(" {:%I:%M:%S %p}", timepoint);
    } else {
        datetime_str += fmt_ns::format(" {:%I:%M %p}", timepoint);
    }
    return datetime_str;
}

std::string getDateTimeStr(time_t timestamp, bool with_seconds = false) {
    // TODO(nahor, 2023-11-20): The following works in MSVC, but not in
    // msys, where std::chrono::time_zone is always UTC
    //
    //   auto datetime = std::chrono::system_clock::from_time_t(timestamp);
    //   if (!ImPlot::GetStyle().UseLocalTime) {
    //       return getDateTimeStr(datetime);
    //   }
    //   const std::chrono::time_zone* tz = std::chrono::current_zone();
    //   auto local_datetime = tz->to_local(datetime);
    //   return getDateTimeStr(local_datetime);

    struct std::tm tm {};
    TimeToTm(timestamp, tm);

    std::string datetime_str;
    if (ImPlot::GetStyle().UseISO8601) {
        datetime_str = fmt_ns::format("{:04}-{:02}-{:02}",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    } else {
        std::array<const char*, 12> month_str{
                "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        datetime_str = fmt_ns::format("{} {}, {}",
                month_str.at(tm.tm_mon), tm.tm_mday, tm.tm_year + 1900);
    }
    if (ImPlot::GetStyle().Use24HourClock) {
        if (with_seconds) {
            datetime_str += fmt_ns::format(" {:02}:{:02}:{:02}",
                    tm.tm_hour, tm.tm_min, tm.tm_sec);
        } else {
            datetime_str += fmt_ns::format(" {:02}:{:02}",
                    tm.tm_hour, tm.tm_min);
        }
    } else if (with_seconds) {
        std::chrono::hours hh{tm.tm_hour};
        datetime_str += fmt_ns::format(" {}:{:02}:{:02} {}",
                std::chrono::make12(hh).count(), tm.tm_min, tm.tm_sec,
                (std::chrono::is_am(hh) ? "am" : "pm"));
    } else {
        std::chrono::hours hh{tm.tm_hour};
        datetime_str += fmt_ns::format(" {}:{:02} {}",
                std::chrono::make12(hh).count(), tm.tm_min,
                (std::chrono::is_am(hh) ? "am" : "pm"));
    }
    datetime_str += ((tm.tm_isdst == 0) ? " (std)" : " (dst)");
    return datetime_str;
}

std::string getDateTimeStr(double timestamp, bool with_seconds = false) {
    return getDateTimeStr(static_cast<time_t>(timestamp), with_seconds);
}

std::string getRelativeDateTimeStr(double duration, bool with_seconds = false) {
    static double timezoneOffset = std::nan("0");
    if (std::isnan(timezoneOffset)) {
        // take the current time
        time_t utc = time(nullptr);
        // convert to clock value (hh:mm...) time
        tm tm{};
        gmtime_s(&tm, &utc);
        // pretend that clock is a local time and convert back to time_t (in standard time)
        tm.tm_isdst = 0;
        time_t local = mktime(&tm);

        // Now we have the time_t for the same clock value, one for UTC, one for local
        // so the difference is the timezone offset
        timezoneOffset = static_cast<double>(utc - local);
    }
    if (ImPlot::GetStyle().UseLocalTime) {
        duration += timezoneOffset;
    }

    // Separate the y/m/d from h/m/s because a month is not an integer number of days, and we don't want fraction of
    // a day to display as a less-than-24h day.
    double date = std::floor(duration / sec_per_day);
    double time = duration - date * sec_per_day;

    double years = std::trunc(date / day_per_year);
    date -= years * day_per_year;

    double months = std::trunc(date / day_per_month);
    date -= months * day_per_month;

    double days = std::trunc(date);

    double hours = std::trunc(time / sec_per_hour);
    time -= hours * sec_per_hour;

    double minutes = std::trunc(time / sec_per_min);
    time -= minutes * sec_per_min;

    double seconds = time;

    std::string duration_str;
    if (with_seconds) {
        duration_str += fmt_ns::format("{}:{:02}:{:02.2}", hours, minutes, seconds);
    } else {
        duration_str += fmt_ns::format("{}:{:02.2}", hours, minutes + seconds / sec_per_min);
    }

    bool hasDate = false;
    std::string date_str{" ("};
    date_str += (duration < 0 ? '-' : '+');
    if (years != 0) {
        date_str += fmt_ns::format("{}y", std::abs(years));
        hasDate = true;
    }
    if (hasDate || (months != 0)) {
        date_str += fmt_ns::format("{}{}mo", (hasDate ? " " : ""), std::abs(months));
        hasDate = true;
    }
    if (hasDate || (days != 0)) {
        date_str += fmt_ns::format("{}{}d", (hasDate ? " " : ""), std::abs(days));
        hasDate = true;
    }
    date_str += ')';

    if (hasDate) {
        duration_str += date_str;
    }

    return duration_str;
}

std::string getDurationStr(double duration, bool with_seconds = false) {
    bool negative = (duration < 0);
    duration = std::abs(duration);

    // Separate the y/m/d from h/m/s because a month is not an integer number of days, and we don't want fraction of
    // a day to display as a less-than-24h day.
    double date = std::trunc(duration / sec_per_day);
    double time = duration - date * sec_per_day;

    double years = std::trunc(date / day_per_year);
    date -= years * day_per_year;

    double months = std::trunc(date / day_per_month);
    date -= months * day_per_month;

    // truncate to remove that extra fraction of a day left in the month
    double days = std::trunc(date);

    double hours = std::trunc(time / sec_per_hour);
    time -= hours * sec_per_hour;

    double minutes = std::trunc(time / sec_per_min);
    time -= minutes * sec_per_min;

    double seconds = time;

    std::string duration_str;

    bool hasDate = false;
    if (negative) {
        if (!duration_str.empty()) {
            duration_str += ' ';
        }
        duration_str += '-';
    }
    if (years != 0) {
        duration_str += fmt_ns::format("{}y ", years);
        hasDate = true;
    }
    if (hasDate || (months != 0)) {
        duration_str += fmt_ns::format("{}mo ", months);
        hasDate = true;
    }
    if (hasDate || (days != 0)) {
        duration_str += fmt_ns::format("{:.3g}d ", days);
        hasDate = true;
    }

    if (with_seconds) {
        duration_str += fmt_ns::format("{}:{:02}:{:02.2}", hours, minutes, seconds);
    } else {
        duration_str += fmt_ns::format("{}:{:02.2}", hours, minutes + seconds / sec_per_min);
    }

    return duration_str;
}

int DurationFormatter(double value, char* buff, int size, void* /*data*/) {
    std::string str = getDurationStr(value, true);
    size = static_cast<int>(std::min(str.size() + 1, static_cast<size_t>(size)));
    std::copy(str.data(), str.data() + size, buff);
    return size;
}

int RelativeDateFormatter(double value, char* buff, int size, void* /*data*/) {
    std::string str = getRelativeDateTimeStr(value, true);
    size = static_cast<int>(std::min(str.size() + 1, static_cast<size_t>(size)));
    std::copy(str.data(), str.data() + size, buff);
    return size;
}

int RelativeTimeFormatter(double value, char* buff, int size, void* /*data*/) {
    std::string str = getRelativeDateTimeStr(value, false);
    size = static_cast<int>(std::min(str.size() + 1, static_cast<size_t>(size)));
    std::copy(str.data(), str.data() + size, buff);
    return size;
}

int DegreeFormatter(double value, char* buff, int size, void* data) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto* window = reinterpret_cast<Window*>(data);
    value = window->convTemperature(value);

    auto [ptr, ec] = std::to_chars(buff, buff + size, value);
    if (ec != std::errc{}) {
        return 0;
    }
    return static_cast<int>(std::distance(ptr, buff));
}

bool matchChar(std::string_view& view, char c) {
    if (view.begin() == view.end()) {
        return false;
    }
    if (view[0] != c) {
        return false;
    }
    view.remove_prefix(1);
    return true;
}

bool matchString(std::string_view& view, const std::string_view s) {
    if (view.begin() == view.end()) {
        return false;
    }
    if (!view.starts_with(s)) {
        return false;
    }
    view.remove_prefix(s.size());
    return true;
}

template <typename T>
bool matchVal(std::string_view& view, T& val) {
    if (view.begin() == view.end()) {
        return false;
    }
    std::from_chars_result from_result = std::from_chars(view.data(), view.data() + view.size(), val);
    if (from_result.ec != std::errc{}) {
        return false;
    }
    view.remove_prefix(from_result.ptr - view.data());
    return true;
}

bool parseLine(std::string_view& view, tm& tm, double& temperature) {
    //                        regex       adhoc
    // Clang 17.0.4 Debug:    350k/s     2074k/s
    // GCC 13.2.0 Debug:      230k/s     1419k/s
    // MSVC 17.8.3 Debug:      20k/s      720k/s
    //
    // Clang 17.0.4 Release: 1340k/s     3780k/s
    // GCC 13.2.0 Release:   1280k/s     3700k/s
    // MSVC 17.8.3 Release:   250k/s     2600k/s

// #define USE_REGEX
#ifdef USE_REGEX

    static const std::regex re_data(R"re("(\d{4})-(\d{2})-(\d{2}) (\d{2}):(\d{2})","(-?\d{1,3}(?:\.\d{1,4}))","(\d{1,3}(?:.\d{1,4}))"[\r\n]*)re",
            std::regex_constants::ECMAScript | std::regex_constants::optimize);

    std::string_view dataLine = view.substr(0, 50);
    std::match_results<std::string_view::const_iterator> match;
    if (!std::regex_search(dataLine.begin(), dataLine.end(), match, re_data, std::regex_constants::match_continuous)) {
        return false;
    }

    view.remove_prefix(match.length());

    std::from_chars(&*match[1].first, &*match[1].second, tm.tm_year);
    tm.tm_year -= 1900;
    std::from_chars(&*match[2].first, &*match[2].second, tm.tm_mon);
    tm.tm_mon -= 1;
    std::from_chars(&*match[3].first, &*match[3].second, tm.tm_mday);
    std::from_chars(&*match[4].first, &*match[4].second, tm.tm_hour);
    std::from_chars(&*match[5].first, &*match[5].second, tm.tm_min);
    tm.tm_sec = 0;

    std::from_chars(&*match[6].first, &*match[6].second, temperature, std::chars_format::fixed);
    // double humidity{};
    // std::from_chars(&*match[7].first, &*match[7].second, humidity, std::chars_format::fixed);

#else  // USE_REGEX

    std::string_view tmp_view = view;
    if (!matchChar(tmp_view, '"')) {
        return false;
    }
    if (!matchVal(tmp_view, tm.tm_year)) {
        return false;
    }
    tm.tm_year -= 1900;
    if (!matchChar(tmp_view, '-')) {
        return false;
    }
    if (!matchVal(tmp_view, tm.tm_mon)) {
        return false;
    }
    tm.tm_mon -= 1;
    if (!matchChar(tmp_view, '-')) {
        return false;
    }
    if (!matchVal(tmp_view, tm.tm_mday)) {
        return false;
    }
    if (!matchChar(tmp_view, ' ')) {
        return false;
    }
    if (!matchVal(tmp_view, tm.tm_hour)) {
        return false;
    }
    if (!matchChar(tmp_view, ':')) {
        return false;
    }
    if (!matchVal(tmp_view, tm.tm_min)) {
        return false;
    }
    tm.tm_sec = 0;
    if (!matchString(tmp_view, "\",\"")) {
        return false;
    }
    if (!matchVal(tmp_view, temperature)) {
        return false;
    }
    if (!matchString(tmp_view, "\",\"")) {
        return false;
    }
    double humidity{};
    if (!matchVal(tmp_view, humidity)) {
        return false;
    }
    if (!matchString(tmp_view, "\"\n")) {
        return false;
    }
    view = tmp_view;

#endif  // !USE_REGEX

    return true;
}

bool parseHeader(std::string_view& view, Degree& degree) {
    static std::regex re_header(R"re("Timestamp","Temperature \(°([CF])\)","Relative Humidity \(%\)"[\r\n]*)re",
            std::regex_constants::ECMAScript | std::regex_constants::icase);

    // Need to read the header (and limit the search)
    std::string_view header = view.substr(0, 100);
    std::match_results<std::string_view::const_iterator> match;
    if (!std::regex_search(header.begin(), header.end(), match, re_header, std::regex_constants::match_continuous)) {
        fmt_println("Failed to find header");
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-goto)
        return false;
    }

    degree = (match[1].str() == "C") ? Degree::Celsius : Degree::Fahrenheit;

    view.remove_prefix(match.length());

    return true;
}

}  // namespace

struct PlotMetadataStandard {
    bool useCelsius{};
    ImPlotRect plot_limits;
    ImVec2 plot_size;
    double density{};
    double lower_X{};
    double upper_X{};

    // NOLINTNEXTLINE(readability-implicit-bool-conversion): !? why?
    auto operator<=>(const PlotMetadataStandard&) const = default;
    bool operator==(const PlotMetadataStandard& rhs) const {
        return (*this <=> rhs) == std::partial_ordering::equivalent;
    }
};

struct PlotMetadataHistogram {
    std::array<time_t, 2> range;
    double offset;
    int bins_x;
    int bins_y;

    auto operator<=>(const PlotMetadataHistogram&) const = default;
};

bool Window::init() {
#ifdef APP_GL_DEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, static_cast<int>(true));
#endif
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    int width = kDefaultWindowWidth;
    int height = width * 9 / 16;

    glfwWindow_ = glfwCreateWindow(width, height, kTitle, nullptr, nullptr);
    if (glfwWindow_ == nullptr) {
        return false;
    }

    glfwMakeContextCurrent(glfwWindow_);
    if (gladLoadGL(glfwGetProcAddress) == 0) {
        fmt_println("Failed to initialize GLAD");
        return false;
    }

    TracyGpuContext;

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    fmt_println("OpenGL version: {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    {
        int x{};
        int y{};
        int z{};
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &x);
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &y);
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &z);
        fmt_println("Max compute work group: {} * {} * {}", x, y, z);

        int storage_count{};
        glGetIntegerv(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS, &storage_count);
        fmt_println("Max compute shader storage block: {}", storage_count);
    }
    {
        GLint encoding{};
        glGetNamedFramebufferAttachmentParameteriv(0, GL_FRONT, GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING, &encoding);
        fmt_println("Default framebuffer encoding: {}", ((encoding == GL_LINEAR) ? "linear" : "srgb"));
    }
    fmt_println("SRGB enabled: {}", (glIsEnabled(GL_FRAMEBUFFER_SRGB) == GL_TRUE));
    {
        float v{};
        glGetFloatv(GL_DEPTH_CLEAR_VALUE, &v);
        fmt_println("Default depth value: {}", v);
    }

    glfwSetWindowUserPointer(glfwWindow_, this);
    glfwSetKeyCallback(glfwWindow_, &Window::keyCallback);

    int flags{};
    glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
    if ((flags & GL_CONTEXT_FLAG_DEBUG_BIT) != 0) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(glDebugCallback, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
    }

    return initGame();
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void Window::keyCallback(GLFWwindow* glfwWindow, int key, int scancode, int action, int mods) {
    // fmt_println("Key: {}, scancode: {}, action: {}, mods: {}",
    //         key, scancode, action, mods);

    auto* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
    window->keyChanged(key, scancode, action, mods);
}

void Window::keyChanged(int key, int /*scancode*/, int action, int mods) {
    if ((key == GLFW_KEY_ENTER) && ((mods & GLFW_MOD_ALT) != 0) && (action == GLFW_PRESS)) {
        show_fullscreen = !show_fullscreen;
        updateFullscreen();
    }

    if ((key == GLFW_KEY_ESCAPE) && (action == GLFW_PRESS) && ((mods & kModMask) == 0)) {
        // We close the window setting if it's open, otherwise we shutdown the app
        if (show_settings_window) {
            show_settings_window = false;
        } else {
            setShouldClose();
        }
    }

    if ((key == GLFW_KEY_GRAVE_ACCENT) && (action == GLFW_PRESS) && ((mods & kModMask) == 0)) {
        show_settings_window = !show_settings_window;
    }

    // if ((key == GLFW_KEY_PAUSE) && (action == GLFW_PRESS) && ((mods & kModMask) == 0)) {
    //     pause = !pause;
    // }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void APIENTRY glDebugCallback(GLenum source, GLenum type,
        unsigned int id, GLenum severity, GLsizei /*length*/, const char* message,
        const void* /*userParam*/) {
    if (id == 131185) {
        // Notification about using VIDEO memory as source for a buffer. Doesn't
        // seem important (notification).
        return;
    }
    if (id == 131218) {
        // Shader recompilation based on GL state. Seems normal at the beginning
        // so ignore for now. A better solution would be to check when and/or
        // how often that happens, in case there is a more serious issue and it
        // keeps recompiling
        return;
    }
    const char* source_str = "unknown";
    switch (source) {
        case GL_DEBUG_SOURCE_API:
            source_str = "GL_DEBUG_SOURCE_API";
            break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
            source_str = "GL_DEBUG_SOURCE_WINDOW_SYSTEM";
            break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER:
            source_str = "GL_DEBUG_SOURCE_SHADER_COMPILER";
            break;
        case GL_DEBUG_SOURCE_THIRD_PARTY:
            source_str = "GL_DEBUG_SOURCE_THIRD_PARTY";
            break;
        case GL_DEBUG_SOURCE_APPLICATION:
            source_str = "GL_DEBUG_SOURCE_APPLICATION";
            break;
        case GL_DEBUG_SOURCE_OTHER:
            source_str = "GL_DEBUG_SOURCE_OTHER";
            break;
        default:
            source_str = "GL_DEBUG_SOURCE_<UNKNOWN>";
            break;
    }

    const char* type_str = "unknown";
    switch (type) {
        case GL_DEBUG_TYPE_ERROR:
            type_str = "GL_DEBUG_TYPE_ERROR";
            break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
            type_str = "GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR";
            break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
            type_str = "GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR";
            break;
        case GL_DEBUG_TYPE_PORTABILITY:
            type_str = "GL_DEBUG_TYPE_PORTABILITY";
            break;
        case GL_DEBUG_TYPE_PERFORMANCE:
            type_str = "GL_DEBUG_TYPE_PERFORMANCE";
            break;
        case GL_DEBUG_TYPE_MARKER:
            type_str = "GL_DEBUG_TYPE_MARKER";
            break;
        case GL_DEBUG_TYPE_PUSH_GROUP:
        case GL_DEBUG_TYPE_POP_GROUP:
            // Those two are for tracing. Not much use for them in logs, and
            // they can be very frequent (10s/100s of times per frame). They
            // are only useful with tools such a RenderDoc or Nsight
            return;
        case GL_DEBUG_TYPE_OTHER:
            type_str = "GL_DEBUG_TYPE_OTHER";
            break;
        default:
            type_str = "GL_DEBUG_TYPE_<UNKNOWN>";
            break;
    }

    const char* severity_str = "unknown";
    switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH:
            severity_str = "GL_DEBUG_SEVERITY_HIGH";
            break;
        case GL_DEBUG_SEVERITY_MEDIUM:
            severity_str = "GL_DEBUG_SEVERITY_MEDIUM";
            break;
        case GL_DEBUG_SEVERITY_LOW:
            severity_str = "GL_DEBUG_SEVERITY_LOW";
            break;
        case GL_DEBUG_SEVERITY_NOTIFICATION:
            severity_str = "GL_DEBUG_SEVERITY_NOTIFICATION";
            break;
        default:
            severity_str = "GL_DEBUG_SEVERITY_<UNKNOWN>";
            break;
    }
    fmt_println(stderr, "Debug message (source: {}, type: {}, id: {}, severity: {}): {}",
            source_str, type_str, id, severity_str, message);
}

bool Window::initGame() {
    // Setup Platform/Renderer backends
    imGui = std::make_unique<ImGuiEnv>();
    imGui->init(this);

    ImPlot::GetStyle().UseLocalTime = true;
    ImPlot::GetStyle().UseISO8601 = true;
    ImPlot::GetStyle().Use24HourClock = true;

    std::array colors{IM_COL32(0x00, 0x00, 0x00, 0xFF), IM_COL32(0xFF, 0x00, 0x00, 0xFF), IM_COL32(0xFF, 0xFF, 0x00, 0xFF), IM_COL32(0xFF, 0xFF, 0xFF, 0xFF)};
    ImPlot::AddColormap("BlackHot", colors.data(), static_cast<int>(colors.size()), false);

    return true;
}

void Window::updateFullscreen() {
    static int win_x{};
    static int win_y{};
    static int win_w{};
    static int win_h{};
    if (show_fullscreen) {
        glfwGetWindowPos(glfwWindow_, &win_x, &win_y);
        glfwGetWindowSize(glfwWindow_, &win_w, &win_h);

        GLFWmonitor* monitor = glfwGetPrimaryMonitor();

        const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());

        fmt_println("Setting monitor to {}x{}", mode->width, mode->height);
        glfwSetWindowMonitor(glfwWindow_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        fmt_println("Restoring window to {}x{}+{}+{}", win_w, win_h, win_x, win_y);
        glfwSetWindowMonitor(glfwWindow_, nullptr, win_x, win_y, win_w, win_h, GLFW_DONT_CARE);
    }
}

void Window::processInput() {
    ZoneScopedN(kTracyZoneInput);
}

void Window::renderGame() {
    ZoneScopedN(kTracyZoneGameRender);
    TracyGpuZone(kTracyZoneGameRender);

    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Window::imGuiComputeMultiComponentItemWidth(size_t component_count) {
    auto component_count_f = static_cast<float>(component_count);
    float total_item_width = ImGui::CalcItemWidth();
    float component_width = std::max(1.0f, (total_item_width - ImGui::GetStyle().ItemInnerSpacing.x * (component_count_f - 1)) / component_count_f);
    // last component might be a bit bigger to account for rounding errors
    float component_last = std::max(1.0f, (total_item_width - (component_width + ImGui::GetStyle().ItemInnerSpacing.x) * (component_count_f - 1)));
    for (size_t i = 0; i < (component_count - 1); ++i) {
        ImGui::PushItemWidth(component_width);
    }
    ImGui::PushItemWidth(component_last);
}

void Window::imGuiHelp(const char* desc) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void Window::renderImGuiStatistics() {
    if (ImGui::CollapsingHeader("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("FPS: %.2f", 1.0f / statistics.delta_time_real.count());
        ImGui::Text("Rendering time: %.2f ms", std::chrono::duration_cast<msec_float>(statistics.delta_time_real).count());
        ImGui::Separator();
        ImGui::Text("Total time: %.3f", std::chrono::duration_cast<sec_float>(statistics.end - statistics.start).count());
        ImGui::Text("Frames: %zu", statistics.frame_count);
        ImGui::Text("Avg FPS: %.2f", static_cast<float>(statistics.frame_count) / std::chrono::duration_cast<sec_float>(statistics.end - statistics.start).count());
        ImGui::Text("Avg rendering: %.2f ms", std::chrono::duration_cast<msec_float>(statistics.end - statistics.start).count() / static_cast<float>(statistics.frame_count));
        ImGui::Separator();
        if (ImGui::Button("Reset")) {
            statistics.start = statistics.end;
            statistics.frame_count = 0;
        }
    }
}

void Window::loadData(const std::filesystem::path& filename) {
    LoadProgress progress{
            .loaded = 0,
            .total = std::filesystem::file_size(filename)};
    loadProgress = progress;

    std::ifstream is(filename);
    std::vector<char> strBuf;
    strBuf.resize(1024ull * 1024ull);
    // NOLINTNEXTLINE(bugprone-string-constructor)
    std::string_view strView{strBuf.data(), 0};

    int line_count = 0;
    time_t prev_time = 0;
    Degree degree{};

// #define WRITE_COPY
#ifdef WRITE_COPY
    std::filesystem::path out_filename = filename;
    out_filename += ".dup";
    std::ofstream os{out_filename};
#endif  // WRITE_COPY

    std::vector<DataPoint> loadedData;
    auto start_time = std::chrono::steady_clock::now();
    while (is.good() && (!shouldClose())) {
        // When debugging, which can be slow since the code is not optimized, limit the time to load
        if ((std::chrono::steady_clock::now() - start_time) >= std::chrono::seconds(5)) {
            // update the progress so the loading window gets closed
            progress.total = progress.loaded;
            loadProgress = progress;
            break;
        }

        is.read(strBuf.data() + strView.size(), static_cast<std::streamsize>(strBuf.size() - strView.size()));
        std::streamsize count = is.gcount();
        if (count == 0) {
            continue;
        }
        progress.loaded += count;
        loadProgress = progress;

        strView = std::string_view{strBuf.data(), strView.size() + count};

        if (degree == Degree::Unknown) {
            // Need to read the header (and limit the search)
#ifdef WRITE_COPY
            std::string_view tmpView = strView;
#endif  // WRITE_COPY
            if (!parseHeader(strView, degree)) {
                fmt_println("Failed to find header");
                // NOLINTNEXTLINE(cppcoreguidelines-avoid-goto)
                goto fail;
            }
#ifdef WRITE_COPY
            os.write(tmpView.data(), tmpView.length() - strView.length());
#endif  // WRITE_COPY
        }

        while (!strView.empty()) {
            tm tm{};
            double temperature{};
#ifdef WRITE_COPY
            std::string_view tmpView = strView;
#endif  // WRITE_COPY
            if (!parseLine(strView, tm, temperature)) {
                if (!is.good()) {
                    // There is no more data to read, we should have matched
                    fmt_println("No regex match in EOF data: {}", strView.substr(0, 100));
                    // NOLINTNEXTLINE(cppcoreguidelines-avoid-goto)
                    goto fail;
                }
                std::string_view::size_type lineEnd = strView.find('\n');
                if (lineEnd != std::string_view::npos) {
                    // We should have matched a line, we have enough characters
                    fmt_println("No match : {}", strView.substr(0, lineEnd));
                    // NOLINTNEXTLINE(cppcoreguidelines-avoid-goto)
                    goto fail;
                }

                // No match, we haven't read the whole line yet
                break;
            }
#ifdef WRITE_COPY
            os.write(tmpView.data(), tmpView.length() - strView.length());
#endif  // WRITE_COPY

            ++line_count;
            prev_time = getSysTime(tm, prev_time);

            // TODO(nahor): should be configurable
            // Skip early data which was done to calibrate the sensor
            if (((tm.tm_year + 1900) <= 2020) && ((tm.tm_mon + 1) <= 01) && (tm.tm_mday <= 20)) {
                continue;
            }

            switch (degree) {
                case Degree::Unknown:  // Not possible
                case Degree::Celsius:  // nothing to do
                    break;
                case Degree::Fahrenheit:
                    temperature = fahrenheitToCelsius(temperature);
                    break;
            }

            if (temperature < -40.0) {
                temperature = std::nan("0");
            }

            if ((!loadedData.empty()) && ((loadedData.back().time + 60) != prev_time)) {
                fmt_println("Unexpected time {} ({}), expected {} ({})",
                        getDateTimeStr(prev_time), prev_time,
                        getDateTimeStr(loadedData.back().time + 60), loadedData.back().time + 60);
            }
            loadedData.push_back({.time = prev_time, .temperature = temperature});
        }

        // Move unprocessed data to the front
        // Don't use std::vector::erase because that would shrink the vector, and growing it back would pointlessly
        // initialized the new data
        std::copy(strView.begin(), strView.end(), strBuf.begin());
    }

    fmt_println("Done: {}/{} - {} lines", progress.loaded, progress.total, line_count);
    fmt_println("Speed: {:.0f} line/s ({} lines in {:.3f} seconds)",
            line_count / std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - start_time).count(),
            line_count,
            std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - start_time).count());
    sensorData = std::move(loadedData);
    return;
fail:
    setShouldClose();
    fmt_println("Failed but matched {}/{} - {} lines", progress.loaded, progress.total, line_count);
}

void Window::computeDataSummary(const PlotMetadataStandard& new_plot,
        const SensorData::iterator& begin, const SensorData::iterator& end, int target_year,
        std::vector<double>& summary_X, std::vector<double>& summary_Avg,
        std::vector<double>& summary_Min, std::vector<double>& summary_Max) const {
    summary_X.clear();
    summary_Avg.clear();
    summary_Min.clear();
    summary_Max.clear();

    auto Bucket = [&](double time) {
        return std::floor(time / new_plot.density);
    };

    size_t sample_count = static_cast<size_t>(Bucket(new_plot.upper_X - new_plot.lower_X)) + 1;
    summary_X.reserve(sample_count);
    summary_Avg.reserve(sample_count);
    summary_Min.reserve(sample_count);
    summary_Max.reserve(sample_count);
    double min_bucket = Bucket(new_plot.lower_X);
    double max_bucket = Bucket(new_plot.upper_X);

    // fmt_println(
    //         "Boundaries:"
    //         "\n\tDensity: {} - (# of datapoints per plotpoint: {})"
    //         "\n\tPx/Bucket: {}"
    //         "\n\tGraph: {} - {} (dur: {})"
    //         "\n\tMeta: {} - {}"
    //         "\n\tBuckets: {} - {}"
    //         "\n\tBucket time: {} - {}",
    //         new_plot.density, new_plot.density / 60,
    //         new_plot.plot_size.x * new_plot.density / new_plot.plot_limits.X.Size(),
    //         getDateTimeStr(new_plot.plot_limits.X.Min, true), getDateTimeStr(new_plot.plot_limits.X.Max, true),
    //         getDurationStr(new_plot.plot_limits.X.Size(), true),
    //         getDateTimeStr(new_plot.lower_X, true),
    //         getDateTimeStr(new_plot.upper_X, true),
    //         min_bucket, max_bucket,
    //         getDateTimeStr(min_bucket * new_plot.density, true),
    //         getDateTimeStr(max_bucket * new_plot.density, true));

    auto it_begin = std::lower_bound(begin, end, min_bucket * new_plot.density,
            [&](const DataPoint& data, double bucket_time) { return static_cast<double>(shiftDate(data.time, target_year)) < bucket_time; });
    auto it_end = std::lower_bound(begin, end, (max_bucket + 1.0) * new_plot.density,
            [&](const DataPoint& data, double bucket_time) { return static_cast<double>(shiftDate(data.time, target_year)) < bucket_time; });
    for (auto& it = it_begin; it < it_end; /**/) {
        double bucket = Bucket(static_cast<double>(shiftDate(it->time, target_year)));
        if (bucket < min_bucket) {
            ++it;
            continue;
        }
        if (bucket > max_bucket) {
            break;
        }

        size_t count = 0;
        double val_avg = 0;
        double val_min = std::numeric_limits<double>::max();
        double val_max = std::numeric_limits<double>::lowest();
        while ((it < it_end) && (Bucket(static_cast<double>(shiftDate(it->time, target_year))) == bucket)) {
            if (!std::isnan(it->temperature)) {
                val_avg += it->temperature;
                val_min = std::min(val_min, it->temperature);
                val_max = std::max(val_max, it->temperature);
                ++count;
            }
            ++it;
        }
        // fmt_println("bucket change: {} -> {}",
        //         bucket, ((it < it_end) ? Bucket(static_cast<double>(it->time - offset)) : std::nan("0")));

        if (count == 0) {
            val_avg = std::nan("0");
            val_min = std::nan("0");
            val_max = std::nan("0");
        } else {
            val_avg /= static_cast<double>(count);
        }
        summary_X.push_back((bucket + 0.5) * new_plot.density - 30);
        summary_Avg.push_back(convTemperature(val_avg));
        summary_Min.push_back(convTemperature(val_min));
        summary_Max.push_back(convTemperature(val_max));
        // fmt_println("{}: [{}-{}] in [{}-{}]", idx, X_min, X_max, getter_data.plot_limits.X.Min, getter_data.plot_limits.X.Max);

        // "it" already incremented by the inner loop
    }
}

void Window::renderImGuiPlotStandardSettings() {
    ImGui::SeparatorText("Standard graph settings");
    ImGui::BeginDisabled(plotType != PlotType::kStandard);
    ImGui::SliderFloat("Scale##StandardScale", &densityScaleFactor, 1.0, 10, "%.0f");
    ImGui::EndDisabled();
}

void Window::plotStandard() {
    if (!ImPlot::BeginPlot("##Temperature", ImMax(ImPlot::GetStyle().PlotMinSize, ImGui::GetContentRegionAvail()), ImPlotFlags_NoLegend)) {
        return;
    }

    std::string temperature_axis = fmt_ns::format("Temperature ({})", (useCelsius ? "°C" : "°F"));
    ImPlot::SetupAxes("Time", temperature_axis.c_str(), 0, ImPlotAxisFlags_AutoFit);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);

    if (loadThread.joinable() || sensorData.empty()) {
        // Either the loading thread is still running (more precisely, it hasn't been reclaimed/joined yet),
        // or there was no data to load
        auto end = std::chrono::system_clock::now();
        auto start = end - std::chrono::years(1);
        ImPlot::SetupAxesLimits(
                static_cast<double>(std::chrono::system_clock::to_time_t(start)),
                static_cast<double>(std::chrono::system_clock::to_time_t(end)),
                useCelsius ? 0 : celsiusToFahrenheit(0), useCelsius ? 20 : celsiusToFahrenheit(20));
        ImPlot::SetupAxisZoomConstraints(ImAxis_X1, 1.0f, std::numeric_limits<double>::max());
        ImPlot::SetupAxisZoomConstraints(ImAxis_Y1, 1.0f, 200.0f);
        ImPlot::EndPlot();
        return;
    }
    static PlotMetadataStandard prev_plot;

    // Can't call GetPlotLimits since that locks the plot size and prevent setting up the constraints.
    // Use the limits from the previous plot instead.
    double width = prev_plot.plot_limits.X.Size();
    if (width == 0) {
        ImPlot::SetupAxisLimits(ImAxis_X1,
                static_cast<double>(sensorData.front().time),
                static_cast<double>(sensorData.back().time),
                ImPlotCond_Always);
    } else {
        ImPlot::SetupAxisLimitsConstraints(ImAxis_X1,
                static_cast<double>(sensorData.front().time) - width / 2,
                static_cast<double>(sensorData.back().time) + width / 2);
    }

    PlotMetadataStandard new_plot;
    new_plot.useCelsius = useCelsius;
    new_plot.plot_limits = ImPlot::GetPlotLimits();
    new_plot.plot_size = ImPlot::GetPlotSize();
    {
        double rawBucketDensity = new_plot.plot_limits.X.Size() / 60.0 / new_plot.plot_size.x;
        // double roundedBucketDensity = std::pow(2.0, std::ceil(std::log2(rawBucketDensity)) + densityBias);
        double roundedBucketDensity = std::ceil(rawBucketDensity) * densityScaleFactor;
        new_plot.density = std::max(1.0, roundedBucketDensity) * 60;
    }
    // "-1" because the plotpoint's time is the average of all the datapoints' time included in that bucket. So it's
    // possible to have the beginning of a bucket outside the plot graph (<limits.X.Min) while its plotpoint is
    // actually visible. If we don't add the bucket before that, the graph will "start" at that datapoint, i.e. it
    // won't be connected on its left side.
    new_plot.lower_X = std::floor(new_plot.plot_limits.X.Min / new_plot.density - 1) * new_plot.density;
    // TODO(nahor): check if the plot_limits include or exclude the max value. The code below assumes it's included
    new_plot.upper_X = std::floor((new_plot.plot_limits.X.Max + new_plot.density) / new_plot.density) * new_plot.density;

    static std::vector<double> summary_X;
    static std::vector<double> summary_Avg;
    static std::vector<double> summary_Min;
    static std::vector<double> summary_Max;
    if (new_plot != prev_plot) {
        prev_plot = new_plot;
        computeDataSummary(new_plot, sensorData.begin(), sensorData.end(), 0, summary_X, summary_Avg, summary_Min, summary_Max);
    }

    ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.5f);
    if (!summary_X.empty()) {
        ImPlot::PlotShaded("Temperature", summary_X.data(), summary_Min.data(), summary_Max.data(), static_cast<int>(summary_X.size()));
        // ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle);
        ImPlot::PlotLine("Temperature", summary_X.data(), summary_Avg.data(), static_cast<int>(summary_X.size()));
    }
    ImPlot::PopStyleVar();

    if (ImPlot::IsPlotHovered()) {
        ImPlotPoint mouse = ImPlot::GetPlotMousePos();
        double mouse_time = mouse.x + new_plot.density / 2.0;
        if ((mouse_time < static_cast<double>(sensorData.front().time))
                || (mouse_time >= (static_cast<double>(sensorData.back().time) + new_plot.density))) {
            // Outside the graph
        } else {
            auto it = std::lower_bound(summary_X.begin(), summary_X.end(), mouse_time);
            // NOLINTNEXTLINE(bugprone-branch-clone)
            if ((it == summary_X.end())) {
                --it;
            } else if ((*it) != mouse_time) {
                // Since we already verified that the mouse is in range, then "end" means "last entry"

                // We didn't find the exact value (which is very likely), so try to take the previous entry
                // (note: we know there must be one since we checked that mouse.x was within the range)
                --it;
            }

            size_t distance = std::distance(summary_X.begin(), it);

            ImPlot::PushPlotClipRect();
            ImDrawList* draw_list = ImPlot::GetPlotDrawList();
            draw_list->AddRectFilled(
                    ImPlot::PlotToPixels(ImPlotPoint(summary_X[distance] - new_plot.density / 2.0, new_plot.plot_limits.Y.Min)),
                    ImPlot::PlotToPixels(ImPlotPoint(summary_X[distance] + new_plot.density / 2.0, new_plot.plot_limits.Y.Max)),
                    IM_COL32(128, 128, 128, 64));
            ImPlot::PopPlotClipRect();

            ImGui::BeginTooltip();
            std::string date = getDateTimeStr(summary_X[distance] - new_plot.density / 2.0 + 30.0);
            if (new_plot.density <= 60.0) {
                ImGui::Text("Time: %s", date.c_str());
            } else {
                ImGui::Text("Time Range:");
                ImGui::Indent();
                ImGui::Text("Start: %s", date.c_str());
                date = getDateTimeStr(summary_X[distance] + new_plot.density / 2.0 - 30.0);
                ImGui::Text("End:   %s", date.c_str());
                ImGui::Unindent();
            }
            ImGui::Text("Avg: %.2f", summary_Avg[distance]);
            ImGui::Text("Min: %.2f", summary_Min[distance]);
            ImGui::Text("Max: %.2f", summary_Max[distance]);
            ImGui::EndTooltip();
        }
    }

    ImPlot::EndPlot();
}

void Window::plotDistribution() {
    ImVec2 plotSize = ImGui::GetContentRegionAvail();
    plotSize.x -= (100 + ImGui::GetStyle().ItemSpacing.x);
    plotSize = ImMax(plotSize, ImPlot::GetStyle().PlotMinSize);

    // We can't use GetPlotLimits/GetPlotSize because this locks the plot, which prevents us from setting the ticks.
    // So use a best/good-enough guess
    static ImPlotRange rangeX(0, sec_per_day);
    static double pixelsX = plotSize.x;

    double bucketSize = static_cast<double>(sec_per_day) / distribution_bin_x;

    static PlotMetadataHistogram prev_plot{};
    PlotMetadataHistogram new_plot{
            .range = distribution_range,
            .offset = std::floor(rangeX.Min / 60.0) * 60.0,
            .bins_x = distribution_bin_x,
            .bins_y = distribution_bin_y,
    };

    static std::vector<int> data;
    static int max_count = 0;
    static ImPlotRange rangeY;
    if (new_plot != prev_plot) {
        prev_plot = new_plot;

        data.clear();
        data.resize(static_cast<size_t>(distribution_bin_x) * static_cast<size_t>(distribution_bin_y));

        rangeY = {std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest()};
        for (const DataPoint& v : sensorData) {
            if (std::isnan(v.temperature)) {
                continue;
            }
            rangeY.Min = ImMin(rangeY.Min, v.temperature);
            rangeY.Max = ImMax(rangeY.Max, v.temperature);
        }
        max_count = 0;
        for (const DataPoint& v : sensorData) {
            if ((v.time < distribution_range[0]) || (v.time > distribution_range[1])) {
                continue;
            }
            int binX = std::clamp(static_cast<int>((static_cast<double>(v.time) - new_plot.offset) / bucketSize) % distribution_bin_x, 0, distribution_bin_x - 1);
            // Note 1: heatmap data must be binned top-to-bottom
            int binY = std::clamp(static_cast<int>((rangeY.Max - v.temperature) * distribution_bin_y / (rangeY.Max - rangeY.Min)), 0, distribution_bin_y - 1);

            size_t offset = binY * distribution_bin_x + binX;
            ++data[offset];
            max_count = ImMax(max_count, data[offset]);
        }
    }

    ImPlot::PushColormap("BlackHot");
    if (ImPlot::BeginPlot("##Heatmap1", plotSize, ImPlotFlags_NoLegend)) {
        constexpr double tickWidth = 100.0;

        // More or less, we want 1 unit per "bucket", with one bucket per `tickWidth` pixels
        double unit = rangeX.Size() / (pixelsX / tickWidth);
        std::array unitList = {
                6 * sec_per_hour,
                3 * sec_per_hour,
                2 * sec_per_hour,
                1 * sec_per_hour,
                30 * sec_per_min,
                15 * sec_per_min,
                10 * sec_per_min,
                5 * sec_per_min,
                2 * sec_per_min,
                1 * sec_per_min};
        size_t unit_index = 0;
        for (size_t i = 1; i < unitList.size(); ++i) {
            // if (unitList.at(i) > unit) {
            if (std::abs(unitList.at(i) - unit) < std::abs(unitList.at(unit_index) - unit)) {
                unit_index = i;
            }
        }
        {
            static size_t oldIndex = 0;
            if (unit_index != oldIndex) {
                fmt_println("New unit: {} - {} aka {} (ref: {})", unit_index, unitList.at(unit_index), getDurationStr(unitList.at(unit_index)), unit);
                oldIndex = unit_index;
            }
        }

        ImPlot::SetupAxes(nullptr, nullptr,
                ImPlotAxisFlags_Foreground,
                ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_Foreground);
        // ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
        ImPlot::SetupAxisFormat(ImAxis_X1, RelativeTimeFormatter, nullptr);
        ImPlot::SetupAxisZoomConstraints(ImAxis_X1, 5 * sec_per_min, 1 * sec_per_day);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, 0, 2.0 * sec_per_day);
        ImPlot::SetupAxisLimits(ImAxis_X1, rangeX.Min, rangeX.Max);

        double min_tick = std::round(rangeX.Min / unitList.at(unit_index)) * unitList.at(unit_index);
        double max_tick = std::round(rangeX.Max / unitList.at(unit_index)) * unitList.at(unit_index);
        int n_ticks = static_cast<int>((max_tick - min_tick) / unitList.at(unit_index) + 1);
        ImPlot::SetupAxisTicks(ImAxis_X1, min_tick, max_tick, n_ticks, nullptr);

        // ImPlot::SetupAxisTicks(ImAxis_X1, 0, sec_per_day * 2, sec_per_day * 2 / unitList.at(unit_index) + 1, nullptr);
        ImPlot::SetupAxisFormat(ImAxis_Y1, DegreeFormatter, this);

        rangeX = ImPlot::GetPlotLimits().X;
        pixelsX = ImPlot::GetPlotSize().x;

        ImPlot::PlotHeatmap("heat", data.data(),
                distribution_bin_y, distribution_bin_x,
                0, max_count,
                nullptr,
                ImPlotPoint(0 + new_plot.offset, rangeY.Min),
                ImPlotPoint(sec_per_day + new_plot.offset, rangeY.Max), ImPlotHeatmapFlags_None);
        ImPlot::EndPlot();
    }
    ImGui::SameLine();
    ImPlot::ColormapScale("Count", 0, max_count, ImVec2(100, 0));
    ImPlot::PopColormap();
}

void Window::plotCompare() {
    if (!ImPlot::BeginPlot("##YearlyCompare", ImMax(ImPlot::GetStyle().PlotMinSize, ImGui::GetContentRegionAvail()), ImPlotFlags_None)) {
        return;
    }

    std::string temperature_axis = fmt_ns::format("Temperature ({})", (useCelsius ? "°C" : "°F"));
    ImPlot::SetupAxes("Time", temperature_axis.c_str(), 0, ImPlotAxisFlags_AutoFit);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);

    // Use 2000 (bissextile) as reference year
    tm tm{.tm_sec = 0, .tm_min = 0, .tm_hour = 0, .tm_mday = 1, .tm_mon = 1 - 1, .tm_year = 2000 - 1900};
    time_t start_time = TmToTime(tm);
    tm = {.tm_sec = 0, .tm_min = 0, .tm_hour = 0, .tm_mday = 1, .tm_mon = 1 - 1, .tm_year = 2001 - 1900};
    time_t end_time = TmToTime(tm);
    ImPlot::SetupAxesLimits(
            static_cast<double>(start_time), static_cast<double>(end_time),
            useCelsius ? 0 : celsiusToFahrenheit(0), useCelsius ? 20 : celsiusToFahrenheit(20));
    ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, static_cast<double>(start_time), static_cast<double>(end_time));
    ImPlot::SetupAxisZoomConstraints(ImAxis_X1, sec_per_min, 366 * sec_per_day);
    ImPlot::SetupAxisZoomConstraints(ImAxis_Y1, 1.0f, 200.0f);

    if (loadThread.joinable() || sensorData.empty()) {
        // Either the loading thread is still running (more precisely, it hasn't been reclaimed/joined yet),
        // or there was no data to load
        ImPlot::EndPlot();
        return;
    }
    static PlotMetadataStandard prev_plot;
    PlotMetadataStandard new_plot;
    new_plot.useCelsius = useCelsius;
    new_plot.plot_limits = ImPlot::GetPlotLimits();
    new_plot.plot_size = ImPlot::GetPlotSize();
    {
        // Because we have multiple plot lines, 1px per plot point is just a mess
        // TODO(nahor): add a plot setting, round to a nearest sensible size (min, 10min, hours, ...)
        static constexpr float density_scale = 20.0f;  // 20px per plot points
        double rawBucketDensity = new_plot.plot_limits.X.Size() / 60.0 / new_plot.plot_size.x * density_scale;
        double roundedBucketDensity = std::ceil(rawBucketDensity) * densityScaleFactor;
        new_plot.density = std::max(1.0, roundedBucketDensity) * 60;
    }
    // "-1" because the plotpoint's time is the average of all the datapoints' time included in that bucket. So it's
    // possible to have the beginning of a bucket outside the plot graph (<limits.X.Min) while its plotpoint is
    // actually visible. If we don't add the bucket before that, the graph will "start" at that datapoint, i.e. it
    // won't be connected on its left side.
    new_plot.lower_X = std::floor(new_plot.plot_limits.X.Min / new_plot.density - 1) * new_plot.density;
    // TODO(nahor): check if the plot_limits include or exclude the max value. The code below assumes it's included
    new_plot.upper_X = std::floor((new_plot.plot_limits.X.Max + new_plot.density) / new_plot.density) * new_plot.density;

    static std::map<int, std::vector<double>> summary_X;
    static std::map<int, std::vector<double>> summary_Avg;
    static std::map<int, std::vector<double>> summary_Min;
    static std::map<int, std::vector<double>> summary_Max;
    if (new_plot != prev_plot) {
        prev_plot = new_plot;

        struct tm tm {};
        TimeToTm(sensorData.front().time, tm);
        tm = {.tm_mday = 1, .tm_year = tm.tm_year};
        auto it = sensorData.begin();
        while (it < sensorData.end()) {
            tm.tm_year++;
            time_t end = TmToTime(tm);
            auto it_end = std::lower_bound(it, sensorData.end(), end, [](const DataPoint& data, time_t end) {
                return data.time < end;
            });
            computeDataSummary(new_plot, it, it_end, 2000, summary_X[tm.tm_year + 1900 - 1], summary_Avg[tm.tm_year + 1900 - 1], summary_Min[tm.tm_year + 1900 - 1], summary_Max[tm.tm_year + 1900 - 1]);
            it = it_end;
        }
    }

    ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.125f);
    if (!summary_X.empty()) {
        for (auto [year, data] : summary_X) {
            std::string year_str = fmt_ns::format("{}", year);

            ImPlot::PlotShaded(year_str.c_str(), data.data(), summary_Min[year].data(), summary_Max[year].data(), static_cast<int>(data.size()));
            // ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle);
            ImPlot::PlotLine(year_str.c_str(), data.data(), summary_Avg[year].data(), static_cast<int>(data.size()));
        }
    }
    ImPlot::PopStyleVar();

    // if (ImPlot::IsPlotHovered()) {
    //     ImPlotPoint mouse = ImPlot::GetPlotMousePos();
    //     double mouse_time = mouse.x + new_plot.density / 2.0;
    //     if ((mouse_time < static_cast<double>(sensorData.front().time))
    //             || (mouse_time >= (static_cast<double>(sensorData.back().time) + new_plot.density))) {
    //         // Outside the graph
    //     } else {
    //         auto it = std::lower_bound(summary_X.begin(), summary_X.end(), mouse_time);
    //         // NOLINTNEXTLINE(bugprone-branch-clone)
    //         if ((it == summary_X.end())) {
    //             --it;
    //         } else if ((*it) != mouse_time) {
    //             // Since we already verified that the mouse is in range, then "end" means "last entry"

    //             // We didn't find the exact value (which is very likely), so try to take the previous entry
    //             // (note: we know there must be one since we checked that mouse.x was within the range)
    //             --it;
    //         }

    //         size_t distance = std::distance(summary_X.begin(), it);

    //         ImPlot::PushPlotClipRect();
    //         ImDrawList* draw_list = ImPlot::GetPlotDrawList();
    //         draw_list->AddRectFilled(
    //                 ImPlot::PlotToPixels(ImPlotPoint(summary_X[distance] - new_plot.density / 2.0, new_plot.plot_limits.Y.Min)),
    //                 ImPlot::PlotToPixels(ImPlotPoint(summary_X[distance] + new_plot.density / 2.0, new_plot.plot_limits.Y.Max)),
    //                 IM_COL32(128, 128, 128, 64));
    //         ImPlot::PopPlotClipRect();

    //         ImGui::BeginTooltip();
    //         std::string date = getDateTimeStr(summary_X[distance] - new_plot.density / 2.0 + 30.0);
    //         if (new_plot.density <= 60.0) {
    //             ImGui::Text("Time: %s", date.c_str());
    //         } else {
    //             ImGui::Text("Time Range:");
    //             ImGui::Indent();
    //             ImGui::Text("Start: %s", date.c_str());
    //             date = getDateTimeStr(summary_X[distance] + new_plot.density / 2.0 - 30.0);
    //             ImGui::Text("End:   %s", date.c_str());
    //             ImGui::Unindent();
    //         }
    //         ImGui::Text("Avg: %.2f", summary_Avg[distance]);
    //         ImGui::Text("Min: %.2f", summary_Min[distance]);
    //         ImGui::Text("Max: %.2f", summary_Max[distance]);
    //         ImGui::EndTooltip();
    //     }
    // }

    ImPlot::EndPlot();
}

void Window::renderImGuiPlotDistributionSettings() {
    ImGui::SeparatorText("Distribution graph settings");
    ImGui::BeginDisabled(plotType != PlotType::kDistribution);
    ImGui::SliderInt("Offset##DistOffset", &distribution_offset_x, 0, min_per_day);

    // Since distribution_bin_x is rounded to the nearest divisor, if we were to use it directly in the slider, it
    // wouldn't possible to use the keyboard or a gamepad since the value would round right back to the old divisor
    // after each incremental change. So use a temporary variable that gets updated only after the last/final change.
    static int temp_bin_x = distribution_bin_x;
    if (ImGui::SliderInt("Number of columns##DistBinX", &temp_bin_x, 1, min_per_day, fmt_ns::format("{}", distribution_bin_x).c_str())) {
        if ((min_per_day % temp_bin_x) == 0) {
            distribution_bin_x = temp_bin_x;
        } else {
            std::array divisors{1, 2, 3, 4, 5, 6, 8, 9, 10, 12, 15, 16, 18, 20, 24, 30, 32, 36, 40, 45, 48, 60, 72, 80, 90, 96, 120, 144, 160, 180, 240, 288, 360, 480, 720, 1440};

            // since we already checked that temp_bin_x is not a divisor,
            // `lower_bound` will necessarily the next highest
            auto high = std::lower_bound(divisors.begin(), divisors.end(), temp_bin_x);  // NOLINT(readability-qualified-auto): "auto*" works with clang but not MSVC
            auto low = high - 1;                                                         // NOLINT(readability-qualified-auto)

            distribution_bin_x = ((temp_bin_x - *low) <= (*high - temp_bin_x)) ? *low : *high;
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        temp_bin_x = distribution_bin_x;
    }
    ImGui::SliderInt("Number of rows##DistBinY", &distribution_bin_y, 1, 1000);

    if (!sensorData.empty()) {
        if (distribution_range == decltype(distribution_range){0, 0}) {
            distribution_range = {sensorData.front().time, sensorData.back().time};
        }
        ImGui::Text("Date range");
        ImGui::Indent();

        // ImGuiSliderFlags_ ImGuiSliderFlags_InlineLabel = ImGuiSliderFlags_None;

        // We don't want the range to be less that one day
        const time_t min_range = sec_per_day;
        ImGui::SetNextItemWidth(-1.0f);
        {
            const time_t min_time = sensorData.front().time;
            const time_t max_time = sensorData.back().time - min_range;
            const std::string str = /**/ getDateTimeStr(distribution_range[0]);  //*/ "Start";
            if (ImGui::SliderScalar("Start##DistRangeStart", ImGuiDataType_S64,
                        &distribution_range[0],  // NOLINT(readability-container-data-pointer)
                        &min_time, &max_time,
                        str.c_str(),
                        ImGuiSliderFlags_InlineLabel)) {
                distribution_range[1] = ImMax(distribution_range[0] + min_range, distribution_range[1]);
            }
        }
        ImGui::SetNextItemWidth(-1.0f);
        {
            const time_t min_time = sensorData.front().time + min_range;
            const time_t max_time = sensorData.back().time;
            const std::string str = /**/ getDateTimeStr(distribution_range[0]);  //*/ "End";
            if (ImGui::SliderScalar("End##DistRangeEnd", ImGuiDataType_S64,
                        &distribution_range[1],
                        &min_time, &max_time,
                        str.c_str(),
                        ImGuiSliderFlags_InlineLabel)) {
                distribution_range[0] = ImMin(distribution_range[0], distribution_range[1] - min_range);
            }
        }
        ImGui::Unindent();
    }

    ImGui::EndDisabled();
}

void Window::renderImGuiMain() {
    bool open = true;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar;
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

#ifdef WITH_IMGUI_DOCKING
    ImGui::SetNextWindowViewport(viewport->ID);
    window_flags |= ImGuiWindowFlags_NoDocking;
#endif  // WITH_IMGUI_DOCKING

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (!ImGui::Begin("DockSpace Demo", &open, window_flags)) {
        setShouldClose();
    }
    ImGui::PopStyleVar(3);

#ifdef WITH_IMGUI_DOCKING
    // If the dockspace doesn't already exist, create it
    ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
    if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_NoDockingOverCentralNode | ImGuiDockNodeFlags_NoUndocking /*| ImGuiDockNodeFlags_NoDockingSplit*/);  // Add empty node
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

        ImGuiID dock_main_id = dockspace_id;  // This variable will track the document node, however we are not using it here as we aren't docking anything into it.
        ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.20f, nullptr, &dock_main_id);

        ImGui::DockBuilderDockWindow("Plot", dock_main_id);
        ImGui::DockBuilderDockWindow("PlotSettings", dock_id_right);
        ImGui::DockBuilderFinish(dockspace_id);
    }
    // Activate the dockspace
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), 0);
#endif  // WITH_IMGUI_DOCKING

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            // Disabling fullscreen would allow the window to be moved to the front of other windows,
            // which we can't undo at the moment without finer window depth/z control.
            ImGui::MenuItem("Settings...", "s", &show_settings_window);
            ImGui::Separator();

            if (ImGui::MenuItem("Exit", "x")) {
                setShouldClose();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    ImGui::End();

#ifdef WITH_IMGUI_DOCKING
    ImGuiWindowClass window_class1;
    window_class1.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoDockingOverMe
            | ImGuiDockNodeFlags_NoDockingOverOther
            | ImGuiDockNodeFlags_NoDockingSplitOther
            | ImGuiDockNodeFlags_NoTabBar;
    ImGui::SetNextWindowClass(&window_class1);
#endif  // WITH_IMGUI_DOCKING
    if (ImGui::Begin("Plot", nullptr, ImGuiWindowFlags_HorizontalScrollbar)) {
        switch (plotType) {
            case PlotType::kStandard:
                plotStandard();
                break;
            case PlotType::kDistribution:
                plotDistribution();
                break;
            case PlotType::kCompare:
                plotCompare();
                break;
        }
    }
    ImGui::End();

    if (ImGui::Begin("PlotSettings", nullptr, ImGuiWindowFlags_None)) {
        ImGui::Checkbox("Local Time", &ImPlot::GetStyle().UseLocalTime);
        ImGui::Checkbox("ISO 8601", &ImPlot::GetStyle().UseISO8601);
        ImGui::Checkbox("24 Hour Clock", &ImPlot::GetStyle().Use24HourClock);
        ImGui::Separator();

        int degree = useCelsius ? 1 : 0;
        ImGui::Text("Temperature unit");
        ImGui::Indent();
        ImGui::RadioButton("°C", &degree, 1);
        ImGui::SameLine();
        ImGui::RadioButton("°F", &degree, 0);
        ImGui::Unindent();
        useCelsius = (degree == 1);

        ImGui::Text("Plot view");
        ImGui::Indent();
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        ImGui::RadioButton("Standard", reinterpret_cast<int*>(&plotType), static_cast<int>(PlotType::kStandard));
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        ImGui::RadioButton("Daily Distribution", reinterpret_cast<int*>(&plotType), static_cast<int>(PlotType::kDistribution));
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        ImGui::RadioButton("Yearly Comparison", reinterpret_cast<int*>(&plotType), static_cast<int>(PlotType::kCompare));

        switch (plotType) {
            case PlotType::kStandard:
                renderImGuiPlotStandardSettings();
                break;
            case PlotType::kDistribution:
                renderImGuiPlotDistributionSettings();
                break;
            case PlotType::kCompare:
                // plotCompare();
                break;
        }

        ImGui::Unindent();
    }
    ImGui::End();

    // if (ImGui::Begin("DurationTestWindow", nullptr, ImGuiWindowFlags_None)) {
    //     ImVec2 plotSize = ImGui::GetContentRegionAvail();
    //     plotSize.x -= (100 + ImGui::GetStyle().ItemSpacing.x);
    //     plotSize.y -= ImGui::GetTextLineHeightWithSpacing();
    //     plotSize = ImMax(plotSize, ImPlot::GetStyle().PlotMinSize);

    //     if (ImPlot::BeginPlot("DurTestPlot", plotSize, ImPlotFlags_NoLegend)) {
    //         static ImPlotRange link{0.0, sec_per_month};

    //         // More or less, we want 1 unit per "bucket", with one bucket per `tickWidth` pixels
    //         double unit = link.Size() / (plotSize.x / 150);
    //         std::array unitList = {
    //                 1 * sec_per_year,
    //                 6 * sec_per_month,
    //                 3 * sec_per_month,
    //                 2 * sec_per_month,
    //                 1 * sec_per_month,
    //                 14 * sec_per_day,
    //                 7 * sec_per_day,
    //                 3 * sec_per_day,
    //                 1 * sec_per_day,
    //                 12 * sec_per_hour,
    //                 6 * sec_per_hour,
    //                 3 * sec_per_hour,
    //                 2 * sec_per_hour,
    //                 1 * sec_per_hour,
    //                 30 * sec_per_min,
    //                 15 * sec_per_min,
    //                 10 * sec_per_min,
    //                 5 * sec_per_min,
    //                 2 * sec_per_min,
    //                 1 * sec_per_min,
    //                 30,
    //                 15,
    //                 5,
    //                 2,
    //                 1,
    //         };
    //         size_t unit_index = 0;
    //         for (size_t i = 1; i < unitList.size(); ++i) {
    //             if (unitList.at(i) > unit) {
    //                 // if (std::abs(unitList.at(i) - unit) < std::abs(unitList.at(unit_index) - unit)) {
    //                 unit_index = i;
    //             }
    //         }
    //         {
    //             static size_t oldIndex = 0;
    //             if (unit_index != oldIndex) {
    //                 // fmt_println("New unit: {} - {} aka {} (ref: {})", unit_index, unitList.at(unit_index), getDurationStr(unitList.at(unit_index)), unit);
    //                 oldIndex = unit_index;
    //             }
    //         }

    //         ImPlot::SetupAxis(ImAxis_X2, nullptr, ImPlotAxisFlags_AuxDefault);

    //         ImPlot::SetupAxisFormat(ImAxis_X1, DurationFormatter, nullptr);
    //         ImPlot::SetupAxisFormat(ImAxis_X2, RelativeDateFormatter, nullptr);

    //         double min_tick = std::round(link.Min / unitList.at(unit_index)) * unitList.at(unit_index);
    //         double max_tick = std::round(link.Max / unitList.at(unit_index)) * unitList.at(unit_index);
    //         int n_ticks = static_cast<int>((max_tick - min_tick) / unitList.at(unit_index) + 1);

    //         ImPlot::SetupAxisTicks(ImAxis_X1, min_tick, max_tick, n_ticks, nullptr);
    //         ImPlot::SetupAxisTicks(ImAxis_X2, min_tick, max_tick, n_ticks, nullptr);

    //         ImPlot::SetupAxisLinks(ImAxis_X1, &link.Min, &link.Max);
    //         ImPlot::SetupAxisLinks(ImAxis_X2, &link.Min, &link.Max);

    //         ImPlot::EndPlot();

    //         ImGui::Text("%s", fmt_ns::format("Min: {}   - Max: {}", link.Min, link.Max).c_str());
    //     }
    // }
    // ImGui::End();

    if (loadThread.joinable()) {
        LoadProgress progress = loadProgress;
        if ((progress.loaded >= progress.total)) {
            loadThread.join();
        } else {
            ImGui::OpenPopup("Loading");

            // Always center this window when appearing
            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(400.0f, 200.0f), ImGuiCond_Appearing);

            if (ImGui::BeginPopupModal("Loading", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Loading...");
                ImGui::ProgressBar(static_cast<float>(progress.loaded) / static_cast<float>(progress.total));
                ImGui::EndPopup();
            }
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void Window::renderImgui() {
    ZoneScopedN(kTracyZoneImgui);
    TracyGpuZone(kTracyZoneImgui);

    // Start the Dear ImGui frame
    imGui->newFrame();

    renderImGuiMain();

    if (show_settings_window) {
        static bool show_gui_demo = false;
        if (show_gui_demo) {
            ImGui::ShowDemoWindow(&show_gui_demo);
        }

        static bool show_plot_demo = false;
        if (show_plot_demo) {
            ImPlot::ShowDemoWindow(&show_plot_demo);
        }

        // Always show the vertical scrollbar. This is to avoid a feedback loop
        // when showing an image and the window nearly requires a vertical
        // scrollbar (see https://github.com/ocornut/imgui/issues/1730)

        if (ImGui::Begin("Settings", &show_settings_window, ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
            ImGui::Checkbox("Show GUI demo window", &show_gui_demo);
            ImGui::Checkbox("Show plot demo window", &show_plot_demo);

            renderImGuiStatistics();
            if (ImGui::CollapsingHeader("Monitor")) {
                if (ImGui::Checkbox("Fullscreen", &show_fullscreen)) {
                    updateFullscreen();
                }

                // if (ImGui::CollapsingHeader("Entities")) {
                //     renderImGuiEntities();
                // }
            }
        }
        ImGui::End();
    }
    imGui->render();
}

void Window::run() {
    // glfwSwapInterval(0);

    statistics.start = std::chrono::steady_clock::now();
    statistics.end = statistics.start;
    statistics.delta_time_real = sec_float{0};
    statistics.frame_count = 0;

    loadThread = std::thread(&Window::loadData, this, kSensorCsvFile);

    while (!shouldClose()) {
        FrameMarkNamed(kTracyFrame);

        {
            ZoneScopedN(kTracyPollEvents);
            glfwPollEvents();
        }

        processInput();

        renderGame();
        renderImgui();
        swapBuffers();

        TracyGpuCollect;

        auto now = std::chrono::steady_clock::now();
        statistics.delta_time_real = std::chrono::duration_cast<sec_float>(now - statistics.end);
        ++statistics.frame_count;
        statistics.end = now;
    }

    fmt_println("Final statistics:");
    fmt_println("Total time: {:.3f} s", std::chrono::duration_cast<sec_float>(statistics.end - statistics.start).count());
    fmt_println("Frames: {}", statistics.frame_count);
    fmt_println("Avg FPS: {:.1f}", static_cast<float>(statistics.frame_count) / std::chrono::duration_cast<sec_float>(statistics.end - statistics.start).count());

    if (loadThread.joinable()) {
        loadThread.join();
    }
}
