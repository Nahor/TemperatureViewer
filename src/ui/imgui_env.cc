#include "imgui_env.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

#include "window.h"

static constexpr const char* kGlslVersion = "#version 330 core";

ImGuiEnv::~ImGuiEnv() {
    ImPlot::SetCurrentContext(plot_ctx);
    ImPlot::DestroyContext(plot_ctx);

    ImGui::SetCurrentContext(gui_ctx);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(gui_ctx);
}

void ImGuiEnv::init(Window* window) {
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    gui_ctx = ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
#ifdef WITH_IMGUI_DOCKING
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#endif  //

    ImGui_ImplGlfw_InitForOpenGL(window->getGlfwWindow(), true);
    ImGui_ImplOpenGL3_Init(kGlslVersion);

    plot_ctx = ImPlot::CreateContext();
}

void ImGuiEnv::newFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiEnv::render() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#ifdef WITH_IMGUI_DOCKING
    if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
        GLFWwindow* main_context = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(main_context);
    }
#endif  // WITH_IMGUI_DOCKING
}

bool ImGuiEnv::wantCaptureMouse() {
    return ImGui::GetIO().WantCaptureMouse;
}
