// spell-checker:words framebuffer fullscreen GLFW
#ifndef WINDOW_H_
#define WINDOW_H_

#include <GLFW/glfw3.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <ctime>
#include <filesystem>
#include <memory>

#include <glm/glm.hpp>
#include <thread>

#include "engine/frame_stat.h"
#include "ui/imgui_env.h"

struct PlotMetadataStandard;

class Window {
 public:
    enum class KeyState {
        kReleased = GLFW_RELEASE,
        kPressed = GLFW_PRESS,
        kRepeat = GLFW_REPEAT,
    };

    enum class PlotType : int {
        kStandard,
        kDistribution,
        kCompare,
    };

    struct DataPoint {
        time_t time;
        double temperature;
    };

    struct LoadProgress {
        uintmax_t loaded{0};
        uintmax_t total{0};
    };

    using SensorData = std::vector<DataPoint>;

 public:
    Window() = default;

    // GLFW doesn't like destroying the last window. It causes a crash when GLFW
    // is terminated. For now, that the only thing that would require a
    // destructor
    ~Window() = default;

    Window(const Window&) = delete;
    Window(Window&&) = delete;
    void operator=(const Window&) = delete;
    void operator=(Window&&) = delete;

    bool init();
    void run();

    GLFWwindow* getGlfwWindow() { return glfwWindow_; }
    void setShouldClose(bool shouldClose = true) {
        glfwSetWindowShouldClose(glfwWindow_, static_cast<int>(shouldClose));
    }
    bool shouldClose() {
        return static_cast<bool>(glfwWindowShouldClose(glfwWindow_));
    }

    KeyState getKey(int key) {
        return static_cast<KeyState>(glfwGetKey(glfwWindow_, key));
    }

    void swapBuffers() { return glfwSwapBuffers(glfwWindow_); }

    double convTemperature(double tempCelsius) const {
        return (useCelsius ? tempCelsius : celsiusToFahrenheit(tempCelsius));
    }

 private:
    static void keyCallback(GLFWwindow* glfwWindow, int key, int scancode, int action, int mods);
    void keyChanged(int key, int scancode, int action, int mods);

    bool initGame();
    void processInput();

    void updateFullscreen();
    void loadData(const std::filesystem::path& filename);
    void computeDataSummary(const PlotMetadataStandard& new_plot,
            const SensorData::iterator& begin, const SensorData::iterator& end, int target_year,
            std::vector<double>& summary_X, std::vector<double>& summary_Avg,
            std::vector<double>& summary_Min, std::vector<double>& summary_Max) const;
    void plotStandard();
    void plotDistribution();
    void plotCompare();

    void renderGame();
    void renderImGuiStatistics();
    void renderImGuiPlotStandardSettings();
    void renderImGuiPlotDistributionSettings();
    void renderImGuiMain();
    void renderImgui();

    static inline double fahrenheitToCelsius(double temperature) {
        return (temperature - 32.0) * 5.0 / 9.0;
    }

    static inline double celsiusToFahrenheit(double temperature) {
        return temperature * 9.0 / 5.0 + 32.0;
    }

    static void imGuiComputeMultiComponentItemWidth(size_t component_count);
    static void imGuiHelp(const char* desc);

 private:
    GLFWwindow* glfwWindow_{nullptr};

    std::unique_ptr<ImGuiEnv> imGui;
    bool show_settings_window{false};
    bool show_fullscreen{false};

    FrameStat statistics;

    std::thread loadThread;
    std::atomic<LoadProgress> loadProgress;
    SensorData sensorData;
    bool useCelsius{true};
    float densityScaleFactor{1.0f};

    PlotType plotType{};

    std::array<time_t, 2> distribution_range{};
    int distribution_offset_x{0};
    int distribution_bin_x{1440};
    int distribution_bin_y{100};
};

#endif  // WINDOW_H_
