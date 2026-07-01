cmake_minimum_required(VERSION 3.16)
project(GoogleTasksImGui VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Packages
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(OpenSSL REQUIRED)

# ImGui submodule
add_library(imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui 
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_GL3W)

# Main Executable
add_executable(google-tasks-imgui 
    src/main.cpp
    src/auth.cpp
    src/api.cpp
)

# Include directories
target_include_directories(google-tasks-imgui PRIVATE
    ${OPENSSL_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Linking
target_link_libraries(google-tasks-imgui PRIVATE
    imgui
    glfw
    OpenGL::GL
    OpenSSL::SSL
    OpenSSL::Crypto
    pthread
    dl
)

# Installation
install(TARGETS google-tasks-imgui DESTINATION bin)

# CPack configuration
set(CPACK_PACKAGE_NAME "google-tasks-imgui")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "developer@example.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Google Tasks C++ Desktop App")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

set(CPACK_GENERATOR "DEB;RPM;TGZ")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libglfw3, libssl3")
set(CPACK_RPM_PACKAGE_REQUIRES "glfw, openssl")

include(CPack)
