#include <GLFW/glfw3.h>
#include <glad/gl.h>
#include <imgui.h>

#include "utils/print.h"
#include "utils/scoped_finally.h"
#include "window.h"

void error_callback(int error, const char *description) {
    fmt_println(stderr, "Error: {} - {}", error, description);
}

int main() {
    glfwInit();
    ScopedFinally glfw_deinit_guard{[] { glfwTerminate(); }};

    glfwSetErrorCallback(error_callback);

    auto window = std::make_unique<Window>();
    if (!window->init()) {
        fmt_println("Failed to create GLFW window");
        return 1;
    }

    window->run();

    return 0;
}
