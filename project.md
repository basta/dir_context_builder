Project: AI Context Builder
1. Objective

The AI Context Builder is a cross-platform desktop utility designed to help developers, researchers, and students quickly aggregate text content from multiple files and directories. The primary goal is to generate a large, consolidated block of text (a "context") that can be easily copied and pasted into Large Language Models (LLMs) for analysis, summarization, or code-related queries.

Given your background in Robotics and AI at CTU, Ondřej, you can think of this as a "context pre-processor" for prompt engineering.
2. Core Features

   Directory Tree View: A navigable file-system tree to visually browse directories.
Y
   Multi-Select: Ability to select multiple files and entire directories.

   Content Aggregation: Recursively reads the content of all selected files and files within selected directories.

   Context Generation: Concatenates all collected text into a single, large string.

   Output Display: A large, read-only text area to display the final generated context.

   Copy to Clipboard: A simple "Copy" button to place the entire context into the system clipboard.

3. Technical Stack

   Language: C++17

   UI Framework: Dear ImGui

   Windowing/Input: SDL2

   Build System: CMake

   Dependency Management: Conan

4. Getting Started: Build Instructions

This guide outlines how to set up and build the project from a fresh clone.
Step 1: Prerequisites

    A C++ Compiler (e.g., MSVC on Windows, GCC/Clang on Linux)

    CMake (version 3.16+)

    Python and Pip (to install Conan)

    Git

Step 2: Install Conan

If you don't have Conan, install it via pip:

pip install conan

Step 3: Clone the Repository

git clone <your-repository-url>
cd AI-Context-Builder

Step 4: Install Dependencies

Run Conan to fetch and build the project dependencies (SDL2, ImGui). We use a profile to ensure the build is configured for the Ninja generator.

# This uses the 'ninja_profile' file in the project root
conan install . --output-folder=build --build=missing --profile=./ninja_profile

Step 5: Configure and Build the Project

Use CMake to configure the project, pointing it to the toolchain file that Conan generated. Then, build the executable.

# Configure the project
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=build/generators/conan_toolchain.cmake

# Build the executable
cmake --build build

The final executable will be located in the build/ directory.

This document was co-authored by Gemini.