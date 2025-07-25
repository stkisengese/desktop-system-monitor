#include "header.h"
#include <SDL.h>

/*
NOTE : You are free to change the code as you wish, the main objective is to make the
       application work and pass the audit.

       It will be provided the main function with the following functions :

       - `void systemWindow(const char *id, ImVec2 size, ImVec2 position)`
            This function will draw the system window on your screen
       - `void memoryProcessesWindow(const char *id, ImVec2 size, ImVec2 position)`
            This function will draw the memory and processes window on your screen
       - `void networkWindow(const char *id, ImVec2 size, ImVec2 position)`
            This function will draw the network window on your screen
*/

// About Desktop OpenGL function loaders:
//  Modern desktop OpenGL doesn't have a standard portable header file to load OpenGL function pointers.
//  Helper libraries are often used for this purpose! Here we are supporting a few common ones (gl3w, glew, glad).
//  You may use another loader/header of your choice (glext, glLoadGen, etc.), or chose to manually implement your own.
#if defined(IMGUI_IMPL_OPENGL_LOADER_GL3W)
#include <GL/gl3w.h> // Initialize with gl3wInit()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLEW)
#include <GL/glew.h> // Initialize with glewInit()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD)
#include <glad/glad.h> // Initialize with gladLoadGL()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD2)
#include <glad/gl.h> // Initialize with gladLoadGL(...) or gladLoaderLoadGL()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLBINDING2)
#define GLFW_INCLUDE_NONE      // GLFW including OpenGL headers causes ambiguity or multiple definition errors.
#include <glbinding/Binding.h> // Initialize with glbinding::Binding::initialize()
#include <glbinding/gl/gl.h>
using namespace gl;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLBINDING3)
#define GLFW_INCLUDE_NONE        // GLFW including OpenGL headers causes ambiguity or multiple definition errors.
#include <glbinding/glbinding.h> // Initialize with glbinding::initialize()
#include <glbinding/gl/gl.h>
using namespace gl;
#else
#include IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#endif

// systemWindow, display information for the system monitorization
void systemWindow(const char *id, ImVec2 size, ImVec2 position)
{
    static SystemInfo sysInfo;
    static auto last_info_update = chrono::steady_clock::now();

    ImGui::Begin(id);
    ImGui::SetWindowSize(id, size);
    ImGui::SetWindowPos(id, position);

    auto now = chrono::steady_clock::now();
    auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - last_info_update);
    if (elapsed.count() > 2000) // Refresh every 2 seconds
    {
        sysInfo = getSystemInfo(); // fetch fresh data from /proc
        last_info_update = now;
    }

    // Display system information 
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 255, 100, 255)); // Light green for headers
    ImGui::Text("System Information");
    ImGui::PopStyleColor();
    ImGui::Separator();

    ImGui::Text("OS: %s", sysInfo.os_name.c_str());
    ImGui::Text("Hostname: %s", sysInfo.hostname.c_str());
    ImGui::Text("User: %s", sysInfo.username.c_str());
    ImGui::Text("CPU: %s", sysInfo.cpu_model.c_str());

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 255, 100, 255));
    ImGui::Text("Process Counts");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Text("Tasks: %d total, %d Running, %d Sleeping, %d Zombie, %d Stopped",
    sysInfo.total_processes, sysInfo.running_processes,
                sysInfo.sleeping_processes, sysInfo.zombie_processes,
                sysInfo.stopped_processes);

    ImGui::Spacing();
    ImGui::Separator();

    // Tabbed interface for performance monitoring 
    if (ImGui::BeginTabBar("PerformanceMonitor"))
    {
        static auto last_update = now;
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 150, 150, 255));
        if (ImGui::BeginTabItem("CPU")) // CPU Tab
        {
            ImGui::PopStyleColor();
            // Update CPU data based on FPS setting
            auto now = chrono::steady_clock::now();
            auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - last_update);
            if (!graph_paused && elapsed.count() >= (1000.0f / graph_fps))
            {
                updateCPUHistory();
                last_update = now;
            }

            renderCPUGraph();
            ImGui::EndTabItem();
        }
        else
        {
            ImGui::PopStyleColor();
        }

        // Fan Tab with color
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 255, 150, 255));
        if (ImGui::BeginTabItem("Fan")) // Fan Tab
        {
            ImGui::PopStyleColor();
            auto now = chrono::steady_clock::now();
            auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - last_update);
            if (!fan_paused && elapsed.count() >= (1000.0f / fan_fps))
            {
                updateFanHistory();
                last_update = now;
            }

            renderFanGraph();
            ImGui::EndTabItem();
        }
        else
        {
            ImGui::PopStyleColor();
        }

        // Thermal Tab with color
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 150, 255, 255));
        if (ImGui::BeginTabItem("Thermal")) // Thermal Tab
        {
            ImGui::PopStyleColor();
            auto now = chrono::steady_clock::now();
            auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - last_update);
            if (!thermal_paused && elapsed.count() >= (1000.0f / thermal_fps))
            {
                updateThermalHistory();
                last_update = now;
            }

            renderThermalGraph();
            ImGui::EndTabItem();
        }
        else
        {
            ImGui::PopStyleColor();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

// memoryProcessesWindow, display information for the memory and processes information
void memoryProcessesWindow(const char *id, ImVec2 size, ImVec2 position)
{
    ImGui::Begin(id);
    ImGui::SetWindowSize(id, size);
    ImGui::SetWindowPos(id, position);

    static vector<Proc> cached_processes;
    static chrono::steady_clock::time_point last_update;
    static bool processes_need_update = true;

    // Check if we need to update process list (every 3 seconds)
    auto now = chrono::steady_clock::now();
    if (processes_need_update ||
        chrono::duration_cast<chrono::seconds>(now - last_update).count() >= 3)
    {
        cached_processes = getAllProcesses();
        last_update = now;
        processes_need_update = false;
    }

    // Memory usage section
    if (ImGui::CollapsingHeader("Memory Usage", ImGuiTreeNodeFlags_DefaultOpen))
    {
        renderMemoryBars();
    }

    ImGui::Separator();

    // Process table section
    if (ImGui::CollapsingHeader("Process Table", ImGuiTreeNodeFlags_DefaultOpen))
    {
        updateProcessCPUData();
        renderProcessTable(cached_processes);
    }

    ImGui::End();
}

// networkWindow function for displaying network information
void networkWindow(const char *id, ImVec2 size, ImVec2 position)
{
    ImGui::Begin(id);
    ImGui::SetWindowSize(id, size);
    ImGui::SetWindowPos(id, position);

    // Update network data periodically
    static auto last_update = chrono::steady_clock::now();
    auto now = chrono::steady_clock::now();
    auto elapsed = chrono::duration_cast<chrono::milliseconds>(now - last_update);

    if (elapsed.count() > 2000)
    { // Update every 2 seconds
        thread([]()
               {
            parseNetworkDevFile();
            getNetworkInterfaces(); })
            .detach();
        last_update = now;
    }

    // Header section with network interfaces overview
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
    ImGui::Text("Network Interfaces");
    ImGui::PopStyleColor();
    ImGui::Separator();
    renderNetworkInterfaces();

    ImGui::Spacing();
    ImGui::Spacing();

    // Main content area with improved tab organization
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
    ImGui::Text("Network Statistics & Usage");
    ImGui::PopStyleColor();
    ImGui::Separator();

    // Create main tab bar with clearer organization
    if (ImGui::BeginTabBar("NetworkMainTabs", ImGuiTabBarFlags_None))
    {
        // Statistics Tab (combines RX and TX tables)
        if (ImGui::BeginTabItem("Network Statistics"))
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
            ImGui::Text("Detailed packet and error statistics for all network interfaces");
            ImGui::PopStyleColor();
            ImGui::Spacing();
            
            // Sub-tabs for RX and TX statistics
            if (ImGui::BeginTabBar("StatisticsTabs", ImGuiTabBarFlags_Reorderable))
            {
                if (ImGui::BeginTabItem("RX (Received)"))
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 0.7f, 1.0f));
                    ImGui::Text("Receive Statistics");
                    ImGui::PopStyleColor();
                    ImGui::Text("Bytes, Packets, Errors, Drops, FIFO, Frame, Compressed, Multicast");
                    ImGui::Spacing();
                    renderRXTable();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("TX (Transmitted)"))
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.7f, 1.0f));
                    ImGui::Text("Transmit Statistics");
                    ImGui::PopStyleColor();
                    ImGui::Text("Bytes, Packets, Errors, Drops, FIFO, Collisions, Carrier, Compressed");
                    ImGui::Spacing();
                    renderTXTable();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
            ImGui::EndTabItem();
        }

        // Usage Visualization Tab (combines RX and TX usage bars)
        if (ImGui::BeginTabItem("Usage Visualization"))
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
            ImGui::Text("Visual representation of network usage (0 - 2GB scale)");
            ImGui::PopStyleColor();
            ImGui::Spacing();
            
            // Sub-tabs for RX and TX usage visualization
            if (ImGui::BeginTabBar("UsageTabs", ImGuiTabBarFlags_Reorderable))
            {
                if (ImGui::BeginTabItem("RX Usage"))
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 0.7f, 1.0f));
                    ImGui::Text("Received Data Usage");
                    ImGui::PopStyleColor();
                    ImGui::Text("Visual display of received bytes (automatically scaled: MB/GB)");
                    ImGui::Spacing();
                    renderRXUsageBars();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("TX Usage"))
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.7f, 1.0f));
                    ImGui::Text("Transmitted Data Usage");
                    ImGui::PopStyleColor();
                    ImGui::Text("Visual display of transmitted bytes (automatically scaled: MB/GB)");
                    ImGui::Spacing();
                    renderTXUsageBars();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
            ImGui::EndTabItem();
        }

        // Real-time monitoring tab
        if (ImGui::BeginTabItem("Real-time Monitor"))
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
            ImGui::Text("Live network activity monitoring");
            ImGui::PopStyleColor();
            ImGui::Spacing();
            
            // Combined real-time view
            ImGui::Columns(2, "RealtimeColumns", true);
            
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 0.7f, 1.0f));
            ImGui::Text("RX Activity");
            ImGui::PopStyleColor();
            renderRXUsageBars();
            
            ImGui::NextColumn();
            
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.7f, 1.0f));
            ImGui::Text("TX Activity");
            ImGui::PopStyleColor();
            renderTXUsageBars();
            
            ImGui::Columns(1);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

// Main code
int main(int, char **)
{
    // Setup SDL
    // (Some versions of SDL before <2.0.10 appears to have performance/stalling issues on a minority of Windows systems,
    // depending on whether SDL_INIT_GAMECONTROLLER is enabled or disabled.. updating to latest version of SDL is recommended!)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }

    // GL 3.0 + GLSL 130
    const char *glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window *window = SDL_CreateWindow("Dear ImGui SDL2+OpenGL3 example", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Initialize OpenGL loader
#if defined(IMGUI_IMPL_OPENGL_LOADER_GL3W)
    bool err = gl3wInit() != 0;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLEW)
    bool err = glewInit() != GLEW_OK;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD)
    bool err = gladLoadGL() == 0;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD2)
    bool err = gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress) == 0; // glad2 recommend using the windowing library loader instead of the (optionally) bundled one.
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLBINDING2)
    bool err = false;
    glbinding::Binding::initialize();
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLBINDING3)
    bool err = false;
    glbinding::initialize([](const char *name)
                          { return (glbinding::ProcAddress)SDL_GL_GetProcAddress(name); });
#else
    bool err = false; // If you use IMGUI_IMPL_OPENGL_LOADER_CUSTOM, your loader is likely to requires some form of initialization.
#endif
    if (err)
    {
        fprintf(stderr, "Failed to initialize OpenGL loader!\n");
        return 1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    // render bindings
    ImGuiIO &io = ImGui::GetIO();

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // background color
    // note : you are free to change the style of the application
    ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    // Main loop
    bool done = false;
    while (!done)
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                done = true;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame(window);
        ImGui::NewFrame();

        {
            ImVec2 mainDisplay = io.DisplaySize;
            memoryProcessesWindow("== Memory and Processes ==",
                                  ImVec2((mainDisplay.x / 2) - 20, (mainDisplay.y / 2) + 30),
                                  ImVec2((mainDisplay.x / 2) + 10, 10));
            // --------------------------------------
            systemWindow("== System ==",
                         ImVec2((mainDisplay.x / 2) - 10, (mainDisplay.y / 2) + 30),
                         ImVec2(10, 10));
            // --------------------------------------
            networkWindow("== Network ==",
                          ImVec2(mainDisplay.x - 20, (mainDisplay.y / 2) - 60),
                          ImVec2(10, (mainDisplay.y / 2) + 50));
        }

        // Rendering
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
