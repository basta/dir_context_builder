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
#include <iostream>

#include "nlohmann/json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;



struct Project
{
    std::string name;
    std::string root_path;
    std::vector<std::string> selected_paths;
};

enum class SelectionState {
    NotSelected,
    PartiallySelected,
    FullySelected
};


// This vector will hold all loaded projects.
static std::vector<Project> projects;
static std::map<std::string, SelectionState> directory_state_cache;




// A recursive helper to determine the state without re-iterating the directory structure multiple times.
void CheckChildrenState(const fs::path& path, const std::map<std::string, bool>& selection, bool& found_selected, bool& found_unselected)
{
    // Stop if we've already found both states, no need to check further.
    if (found_selected && found_unselected) {
        return;
    }

    for (const auto& entry : fs::directory_iterator(path))
    {
        try {
            std::string entry_path_str = entry.path().string();
            if (entry.is_directory())
            {
                CheckChildrenState(entry.path(), selection, found_selected, found_unselected);
            }
            else // It's a file
            {
                // Check if the file is in the selection map and if it's selected
                if (selection.count(entry_path_str) && selection.at(entry_path_str)) {
                    found_selected = true;
                } else {
                    found_unselected = true;
                }
            }
        } catch (std::exception e) {
            std::cerr << "Filesystem error: " << e.what() << std::endl;
            continue;
        }
    }
}

SelectionState GetDirectorySelectionState(const fs::path& path, const std::map<std::string, bool>& selection)
{
    bool found_selected = false;
    bool found_unselected = false;

    try {
        if (!fs::exists(path) || !fs::is_directory(path) || fs::is_empty(path)) {
            // An empty directory can't be partially selected.
            // We check its own state in the selection map.
            return selection.count(path.string()) && selection.at(path.string()) ? SelectionState::FullySelected : SelectionState::NotSelected;
        }
        CheckChildrenState(path, selection, found_selected, found_unselected);
    } catch (const fs::filesystem_error& e) {
        // Handle potential permission errors gracefully
        return SelectionState::NotSelected;
    }

    if (found_selected && found_unselected) {
        return SelectionState::PartiallySelected;
    }
    if (found_selected) {
        return SelectionState::FullySelected;
    }
    return SelectionState::NotSelected;
}



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
    directory_state_cache.erase(path.string()); // Invalidate this directory's cache

    if (fs::is_directory(path))
    {
        for (const auto& entry : fs::directory_iterator(path))
        {
            SetSelectionRecursively(entry.path(), selected, selection);
        }
    }
}


void InvalidateParentCaches(const fs::path& path)
{
    fs::path current = path;
    while (current.has_parent_path() && !current.parent_path().empty())
    {
        directory_state_cache.erase(current.parent_path().string());
        current = current.parent_path();
    }
}

SelectionState CalculateAndCacheDirectoryState(const fs::path& path, const std::map<std::string, bool>& selection)
{
    try {
        std::string path_str = path.string();

        // 1. Check cache first - This is the key optimization!
        if (directory_state_cache.count(path_str))
        {
            return directory_state_cache.at(path_str);
        }

        // --- If not in cache, calculate it ---
        bool found_selected = false;
        bool found_unselected = false;

        try
        {
            if (fs::is_empty(path)) {
                // Base case for empty directory
                directory_state_cache[path_str] = SelectionState::NotSelected;
                return SelectionState::NotSelected;
            }

            for (const auto& entry : fs::directory_iterator(path))
            {
                if (found_selected && found_unselected) break; // Early exit

                if (entry.is_directory())
                {
                    // Recursively call this function to ensure children are cached
                    SelectionState child_state = CalculateAndCacheDirectoryState(entry.path(), selection);
                    if (child_state != SelectionState::NotSelected) found_selected = true;
                    if (child_state != SelectionState::FullySelected) found_unselected = true;
                }
                else // It's a file
                {
                    if (selection.count(entry.path().string()) && selection.at(entry.path().string())) {
                        found_selected = true;
                    } else {
                        found_unselected = true;
                    }
                }
            }
        }
        catch (const fs::filesystem_error&) { /* Ignore errors */ }

        SelectionState result = SelectionState::NotSelected;
        if (found_selected && found_unselected) {
            result = SelectionState::PartiallySelected;
        } else if (found_selected) {
            result = SelectionState::FullySelected;
        }

        // 2. Store the result in the cache before returning
        directory_state_cache[path_str] = result;
        return result;
    }  catch (std::exception e) {
        std::cerr << "Error calculating directory state: " << e.what() << std::endl;
        return SelectionState::NotSelected;
    }
}

void DrawDirectoryTree(const fs::path& path, std::map<std::string, bool>& selection)
{
    std::vector<fs::directory_entry> directories;
    std::vector<fs::directory_entry> files;

    // Separate directories and files to display directories first
    try {
        for (const auto& entry : fs::directory_iterator(path))
        {
            if (entry.is_directory()) {
                directories.push_back(entry);
            } else {
                files.push_back(entry);
            }
        }
    } catch (const fs::filesystem_error& e) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Error accessing %s", path.string().c_str());
        return;
    }


    // Sort both lists alphabetically
    std::sort(directories.begin(), directories.end(), [](const auto& a, const auto& b) {
        return a.path().filename() < b.path().filename();
    });
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.path().filename() < b.path().filename();
    });

    // --- Render Directories with 3-state logic ---
    for (const auto& entry : directories)
    {
        auto filename = entry.path().filename().string();
        SelectionState state = CalculateAndCacheDirectoryState(entry.path(), selection);

        const char* icon = "[ ]";
        if (state == SelectionState::FullySelected) {
            icon = "[X]"; // Use a capital X instead of ✓
        } else if (state == SelectionState::PartiallySelected) {
            icon = "[~]";
        }

        ImGui::TextUnformatted(icon);
        ImGui::SameLine();

        // Make the icon clickable
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - ImGui::CalcTextSize(icon).x - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::InvisibleButton(filename.c_str(), ImGui::CalcTextSize(icon)))
        {
            // When clicked, a partial or unselected folder becomes fully selected.
            // A fully selected folder becomes unselected.
            bool new_selection_state = (state == SelectionState::NotSelected);
            SetSelectionRecursively(entry.path(), new_selection_state, selection);
            InvalidateParentCaches(entry.path());
        }
        ImGui::SameLine();
        // --- End of Custom Checkbox ---

        if (ImGui::TreeNode(filename.c_str()))
        {
            DrawDirectoryTree(entry.path(), selection);
            ImGui::TreePop();
        }
    }

    // --- Render Files with normal checkbox ---
    for (const auto& entry : files)
    {
        std::string path_string = entry.path().string();
        auto filename = entry.path().filename().string();
        bool& is_selected = selection[path_string];

        // Choose the icon based on the file's selection state
        const char* icon = is_selected ? "[X]" : "[ ]";

        ImGui::TextUnformatted(icon);
        ImGui::SameLine();

        // Make the icon clickable to toggle selection
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - ImGui::CalcTextSize(icon).x - ImGui::GetStyle().ItemSpacing.x);
        // Use the path_string for a unique ID for the InvisibleButton
        if (ImGui::InvisibleButton(path_string.c_str(), ImGui::CalcTextSize(icon)))
        {
            is_selected = !is_selected;
            InvalidateParentCaches(entry.path());
        }
        ImGui::SameLine();

        // Display the filename as simple text
        ImGui::TextUnformatted(filename.c_str());
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
				static char project_name_buffer[128] = "";

                std::vector<const char*> project_names;
                for (const auto& p : projects) {
                    project_names.push_back(p.name.c_str());
                }

                if (ImGui::Combo("##ProjectCombo", &current_project_idx, project_names.data(), project_names.size()))
                {
					// Auto-load project when selected from dropdown
					if (current_project_idx >= 0 && current_project_idx < projects.size())
					{
						const auto& p = projects[current_project_idx];
						strncpy_s(path_buffer, p.root_path.c_str(), sizeof(path_buffer) - 1);
						strncpy_s(project_name_buffer, p.name.c_str(), sizeof(project_name_buffer) - 1);

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
						project_name_buffer[0] = '\0'; // Clear buffer
                    }
                }

				ImGui::InputText("Project Name", project_name_buffer, sizeof(project_name_buffer));

				const bool is_overwrite_mode = (current_project_idx >= 0 && current_project_idx < projects.size() && projects[current_project_idx].name == project_name_buffer);
				const char* save_button_text = is_overwrite_mode ? "Overwrite" : "Save New";

				if (ImGui::Button(save_button_text))
				{
					if (strlen(project_name_buffer) > 0)
					{
						if (is_overwrite_mode)
						{
							// Overwrite existing project
							Project& p = projects[current_project_idx];
							p.root_path = path_buffer;
							p.selected_paths.clear();
							for (const auto& [path, selected] : selection)
							{
								if (selected) {
									p.selected_paths.push_back(path);
								}
							}
						}
						else
						{
							// Save as a new project or update one with the same name
							auto it = std::find_if(projects.begin(), projects.end(), [&](const Project& p) {
								return p.name == project_name_buffer;
								});

							if (it != projects.end()) {
								// A project with this name already exists, update it
								it->root_path = path_buffer;
								it->selected_paths.clear();
								for (const auto& [path, selected] : selection) {
									if (selected) {
										it->selected_paths.push_back(path);
									}
								}
								current_project_idx = static_cast<int>(std::distance(projects.begin(), it));
							}
							else {
								// No project with this name, create a new one
								Project new_project;
								new_project.name = project_name_buffer;
								new_project.root_path = path_buffer;
								for (const auto& [path, selected] : selection) {
									if (selected) {
										new_project.selected_paths.push_back(path);
									}
								}
								projects.push_back(new_project);
								current_project_idx = static_cast<int>(projects.size() - 1);
							}
						}
						SaveProjects();
					}
				}
                ImGui::Separator();
            }

            ImGui::InputText("Path", path_buffer, sizeof(path_buffer));
            ImGui::SameLine();
            if (ImGui::Button("Recalculate States"))
            {
                directory_state_cache.clear();
            }

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
