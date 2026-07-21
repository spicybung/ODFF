cmake_minimum_required(VERSION 3.21)

cmake_policy(SET CMP0091 NEW)

project(ODFF VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(MSVC)
    set(
        CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
endif()

include(FetchContent)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw imgui)

find_package(OpenGL REQUIRED)

add_executable(ODFF
    src/main.cpp
    src/Application.cpp
    src/Camera.cpp
    src/CollisionBuilder.cpp
    src/DffExporter.cpp
    src/FileDialog.cpp
    src/ModelDocument.cpp
    src/OpenGLRenderer.cpp
    src/RenderWareReader.cpp
    src/TxdReader.cpp

    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl2.cpp
)

target_include_directories(ODFF PRIVATE
    src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(ODFF PRIVATE
    glfw
    OpenGL::GL
)

if(WIN32)
    target_link_libraries(ODFF PRIVATE
        comdlg32
        shell32
        ole32
    )

    target_compile_definitions(ODFF PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

if(MSVC)
    target_compile_options(ODFF PRIVATE
        /W4
        /permissive-
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
    )

    target_link_options(ODFF PRIVATE
        $<$<CONFIG:Release>:/LTCG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
        $<$<CONFIG:Release>:/INCREMENTAL:NO>
    )
else()
    target_compile_options(ODFF PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

set_target_properties(ODFF PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
)

add_custom_command(
    TARGET ODFF
    POST_BUILD
    COMMAND
        ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_BINARY_DIR}/Standalone"
    COMMAND
        ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:ODFF>"
        "${CMAKE_BINARY_DIR}/Standalone/ODFF.exe"
    COMMENT
        "Creating standalone ODFF.exe"
)

install(
    TARGETS ODFF
    RUNTIME DESTINATION .
)
