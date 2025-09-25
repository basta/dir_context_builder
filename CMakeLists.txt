# CMake minimum version requirement
cmake_minimum_required(VERSION 4.0)

# Project name and language
project(AIContextBuilder CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find the packages provided by Conan
# The CMakeDeps generator creates the necessary find-modules.
find_package(imgui REQUIRED)
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(nlohmann_json REQUIRED)

# --- Define the Executable ---
add_executable(${PROJECT_NAME}
        src/main.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/../bindings/imgui_impl_sdl2.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/../bindings/imgui_impl_sdl2.h
        ${CMAKE_CURRENT_BINARY_DIR}/../bindings/imgui_impl_opengl3.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/../bindings/imgui_impl_opengl3.h
)


# Add include directories (though find_package often handles this)
message(status "imgui include dirs: ${imgui_INCLUDE_DIRS}")
target_include_directories(${PROJECT_NAME} PRIVATE
        ${imgui_INCLUDE_DIRS}
        ${CMAKE_CURRENT_BINARY_DIR}/../bindings
)


# --- Link Libraries ---
# Link against the targets created by Conan's CMakeDeps generator.
# These targets handle all the necessary include directories and library paths.
target_link_libraries(${PROJECT_NAME} PRIVATE
        imgui::imgui
        SDL2::SDL2
        SDL2::SDL2main
        OpenGL::GL
        nlohmann_json::nlohmann_json
)

# Set the subsystem to WINDOWS for a GUI application (hides the console on Windows)
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE ON)
endif()

