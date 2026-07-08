cmake_minimum_required(VERSION 3.16)
project(GoogleTasksImGui VERSION 1.1.1 LANGUAGES CXX C)

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
add_executable(google-notes-app
    src/main.cpp
    src/auth.cpp
    src/api.cpp
    src/keep_auth.cpp
    src/keep_api.cpp
)

# Include directories
target_include_directories(google-notes-app PRIVATE
    ${OPENSSL_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Linking
target_link_libraries(google-notes-app PRIVATE
    imgui
    glfw
    OpenGL::GL
    OpenSSL::SSL
    OpenSSL::Crypto
    pthread
    dl
)

# Installation
install(TARGETS google-notes-app DESTINATION bin)
install(DIRECTORY assets/fonts DESTINATION share/google-notes-app)

# Desktop integration: launcher entry and hicolor icons
install(FILES assets/google-notes-app.desktop DESTINATION share/applications)
foreach(icon_size 64 256 512)
    install(FILES assets/icons/GN-${icon_size}.png
        DESTINATION share/icons/hicolor/${icon_size}x${icon_size}/apps
        RENAME google-notes-app.png)
endforeach()

# Bundle the OAuth client credentials for zero-config login, if provided.
# The file is gitignored; supply it locally or inject it in CI before packaging.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/client_secret.json")
    install(FILES client_secret.json DESTINATION share/google-notes-app)
else()
    message(WARNING "client_secret.json not found; the package will require users to supply their own OAuth credentials.")
endif()

# CPack configuration
set(CPACK_PACKAGE_NAME "google-notes-app")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "developer@example.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Notes App - desktop client for Google Tasks and Keep")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

set(CPACK_GENERATOR "DEB;RPM;TGZ")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libglfw3, libssl3")
set(CPACK_RPM_PACKAGE_REQUIRES "glfw, openssl")

include(CPack)
