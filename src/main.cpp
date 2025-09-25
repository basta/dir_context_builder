#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include <string>
#include <map>
#include <fstream>
#include <vector>
#include <algorithm>

#include <filesystem>
#include "nlohmann/json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

struct Project
{
    std::string name;
    std::string root_path;
    std::vector<std::string> selected_paths;
};

// This vector will hold all loaded projects.
static std::vector<Project> projects;

void SaveProjects()
{
    json j;
    for (const auto& p : projects)
    {
        j["projects"].push_back({
            {"name", p.name},
            {"root_path", p.root_path},
            {"selected_paths", p.selected_paths}
        });
    }

    std::ofstream o("projects.json");
    o << std::setw(4) << j << std::endl;
}

void LoadProjects()
{
    projects.clear();
    std::ifstream i("projects.json");
    if (!i.is_open()) {
        return; // No projects file yet, which is fine.
    }

    json j;
    i >> j;

    if (j.contains("projects"))
    {
        for (const auto& item : j["projects"])
        {
            Project p;
            p.name = item.value("name", "Unnamed");
            p.root_path = item.value("root_path", ".");
            if (item.contains("selected_paths")) {
                p.selected_paths = item["selected_paths"].get<std::vector<std::string>>();
            }
            projects.push_back(p);
        }
    }
}

void SetSelectionRecursively(const fs::path& path, bool selected, std::map<std::string, bool>& selection)
{
    selection[path.string()] = selected;
    if (fs::is_directory(path))
    {
        for (const auto& entry : fs::directory_iterator(path))
        {
            SetSelectionRecursively(entry.path(), selected, selection);
        }
    }
}

void DrawDirectoryTree(const fs::path& path, std::map<std::string, bool>& selection)
{
    std::vector<fs::directory_entry> directories;
    std::vector<fs::directory_entry> files;

    for (const auto& entry : fs::directory_iterator(path))
    {
        if (entry.is_directory())
        {
            directories.push_back(entry);
        }
        else
        {
            files.push_back(entry);
        }
    }

    std::sort(directories.begin(), directories.end(), [](const auto& a, const auto& b) {
        return a.path().filename() < b.path().filename();
    });

    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.path().filename() < b.path().filename();
    });

    for (const auto& entry : directories)
    {
        std::string path_string = entry.path().string();
        bool& is_selected = selection[path_string];
        auto filename = entry.path().filename().string();

        if (ImGui::Checkbox(("##" + filename).c_str(), &is_selected))
        {
            SetSelectionRecursively(entry.path(), is_selected, selection);
        }
        ImGui::SameLine();
        if (ImGui::TreeNode(filename.c_str()))
        {
            DrawDirectoryTree(entry.path(), selection);
            ImGui::TreePop();
        }
    }

    for (const auto& entry : files)
    {
        std::string path_string = entry.path().string();
        bool& is_selected = selection[path_string];
        auto filename = entry.path().filename().string();

        ImGui::Checkbox(filename.c_str(), &is_selected);
    }
}

void GenerateContext(const std::map<std::string, bool>& selection, std::string& aggregated_text, int& file_count, int& token_count)
{
    aggregated_text.clear();
    file_count = 0;
    token_count = 0;

    for (const auto& [path, selected] : selection)
    {
        if (selected && fs::is_regular_file(path))
        {
            std::ifstream file(path);
            if (file.is_open())
            {
                std::string file_content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                aggregated_text += "--- " + path + " ---\n";
                aggregated_text += file_content;
                aggregated_text += "\n";
                file_count++;
                token_count += file_content.length() / 4; // Simple token approximation
            }
        }
    }
}

// Main application loop
int main(int, char**)
{
    // --- Setup SDL ---
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }

    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100
    const char* glsl_version = "#version 100";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
    // GL 3.2 Core + GLSL 150
    const char* glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("AI Context Builder", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // --- Setup Dear ImGui context ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Our state
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    std::string aggregated_text;
    static char path_buffer[1024] = ".";
    static std::map<std::string, bool> selection;
    int file_count = 0;
    int token_count = 0;

    LoadProjects();

    // --- Main loop ---
    bool done = false;
    while (!done)
    {
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
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // --- Main Application Window ---
        {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("AI Context Builder", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

            // Left Panel (Directory Tree)
            ImGui::BeginChild("LeftPanel", ImVec2(io.DisplaySize.x * 0.3f, 0), true);

            {
                ImGui::Text("Projects");

                static int current_project_idx = -1;
                // Helper to create a C-style string list for the Combo box
                std::vector<const char*> project_names;
                for(const auto& p : projects) {
                    project_names.push_back(p.name.c_str());
                }

                if (ImGui::Combo("##ProjectCombo", &current_project_idx, project_names.data(), project_names.size()))
                {
                    // User selected a new item
                }

                ImGui::SameLine();
                if (ImGui::Button("Load"))
                {
                    if (current_project_idx >= 0 && current_project_idx < projects.size())
                    {
                        const auto& p = projects[current_project_idx];
                        strncpy_s(path_buffer, p.root_path.c_str(), sizeof(path_buffer) - 1);

                        selection.clear();
                        for (const auto& path : p.selected_paths) {
                            selection[path] = true;
                        }
                    }
                }

                ImGui::SameLine();
                if (ImGui::Button("Delete"))
                {
                    if (current_project_idx >= 0 && current_project_idx < projects.size())
                    {
                        projects.erase(projects.begin() + current_project_idx);
                        SaveProjects();
                        current_project_idx = -1; // Reset selection
                    }
                }


                static char new_project_name_buffer[128] = "";
                ImGui::InputText("New Project Name", new_project_name_buffer, sizeof(new_project_name_buffer));
                if (ImGui::Button("Save New"))
                {
                    if (strlen(new_project_name_buffer) > 0)
                    {
                        Project new_project;
                        new_project.name = new_project_name_buffer;
                        new_project.root_path = path_buffer;
                        for (const auto& [path, selected] : selection)
                        {
                            if (selected) {
                                new_project.selected_paths.push_back(path);
                            }
                        }
                        projects.push_back(new_project);
                        SaveProjects();
                        strcpy_s(new_project_name_buffer, ""); // Clear the buffer
                    }
                }
                ImGui::Separator();
            }

            ImGui::InputText("Path", path_buffer, sizeof(path_buffer));

            ImGui::BeginChild("DirectoryTree", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true);
            if (fs::exists(path_buffer))
            {
                DrawDirectoryTree(path_buffer, selection);
            }
            ImGui::EndChild();

            if (ImGui::Button("Generate Context", ImVec2(-1, 0)))
            {
                GenerateContext(selection, aggregated_text, file_count, token_count);
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // Right Panel (Content Display)
            ImGui::BeginChild("ContentDisplay", ImVec2(0, 0), true);
            if (ImGui::Button("Copy to Clipboard"))
            {
                ImGui::SetClipboardText(aggregated_text.c_str());
            }
            ImGui::SameLine();
            ImGui::Text("Files: %d | Tokens: %d", file_count, token_count);

            ImGui::InputTextMultiline("##source", &aggregated_text[0], aggregated_text.size(), ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
            ImGui::EndChild();

            ImGui::End();
        }


        // Rendering
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // --- Cleanup ---
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
