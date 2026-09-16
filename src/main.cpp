#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include "core/gl_compat.h"
#include "core/log.h"
#include "ui/editor_app.h"
#include "ui/icon_font.h"
#include "ui/log_panel.h"

int main(int argc, char** argv) {
    // Relative to the current working directory, which is the exe's own
    // folder for both a Linux terminal launch from that folder and a
    // Windows double-click - see core/log.h. This is the file to grab when
    // asking someone to send you a repro, without needing them to run from
    // a terminal at all.
    core::Log::Init("timeditor.log");

    if (!glfwInit()) {
        core::Log::Error("glfwInit() failed - no usable OpenGL context could be created.");
        return -1;
    }

    GLFWwindow* window = glfwCreateWindow(1280, 720, "TIM Editor", NULL, NULL);
    if (!window) {
        core::Log::Error("glfwCreateWindow() failed.");
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // Logged unconditionally (not just on failure) since the whole point is
    // to tell driver/VM setups apart when the 3D viewport misbehaves without
    // crashing - see docs/ARCHITECTURE.md and gfx::gl::LoadGLExtensions().
    core::Log::Info("GL Vendor:   %s", glGetString(GL_VENDOR));
    core::Log::Info("GL Renderer: %s", glGetString(GL_RENDERER));
    core::Log::Info("GL Version:  %s", glGetString(GL_VERSION));
    core::Log::Info("GL GLSL:     %s", glGetString(GL_SHADING_LANGUAGE_VERSION));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    ui::LoadFonts();

    ui::EditorApp app;
    app.Initialize();

    // Command-line args (e.g. several .tim files selected in a file manager
    // and opened with this app at once) load right at startup.
    for (int i = 1; i < argc; i++) {
        app.LoadFile(argv[i]);
    }

    // Clicking the OS window's close button would otherwise quit instantly
    // with no chance to save; intercept it and route through the same
    // unsaved-changes check as File > Exit instead.
    glfwSetWindowUserPointer(window, &app);
    glfwSetWindowCloseCallback(window, [](GLFWwindow* w) {
        glfwSetWindowShouldClose(w, GLFW_FALSE);
        static_cast<ui::EditorApp*>(glfwGetWindowUserPointer(w))->RequestExit();
    });

    // Dragging one or more .tim files onto the window loads all of them.
    glfwSetDropCallback(window, [](GLFWwindow* w, int count, const char** paths) {
        ui::EditorApp* app_ptr = static_cast<ui::EditorApp*>(glfwGetWindowUserPointer(w));
        for (int i = 0; i < count; i++) {
            app_ptr->LoadFile(paths[i]);
        }
    });

    while (!glfwWindowShouldClose(window) && !app.ShouldQuit()) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.RenderFrame();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
