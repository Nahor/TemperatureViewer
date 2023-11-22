#ifndef UI_IMGUI_ENV_H_
#define UI_IMGUI_ENV_H_

#include <imgui.h>
#include <implot.h>

class Window;

class ImGuiEnv {
 public:
    ImGuiEnv() = default;
    ~ImGuiEnv();

    ImGuiEnv(const ImGuiEnv &) = delete;
    ImGuiEnv(ImGuiEnv &&) = delete;
    void operator=(const ImGuiEnv &) = delete;
    void operator=(ImGuiEnv &&) = delete;

    void init(Window *window);

    void newFrame();
    void render();

    bool wantCaptureMouse();

 private:
    ImGuiContext *gui_ctx{nullptr};
    ImPlotContext *plot_ctx{nullptr};
};

#endif  // UI_IMGUI_ENV_H_
