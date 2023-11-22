cmake_minimum_required(VERSION 3.26.0)
project(sensor VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TRACY_ENABLE "Enable Tracy" OFF)
# add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
if(MSVC)
add_link_options(-fsanitize=address /DEBUG:FULL)
endif()

include(CTest)
enable_testing()

find_program(ccache ccache)
if(ccache)
    message(STATUS "Found ccache... ${ccache}")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ${ccache})
endif()

include(FetchContent)
find_package(Threads REQUIRED)

#### fmt (string formating)
#set(FMT_SYSTEM_HEADERS ON CACHE INTERNAL "Expose headers with marking them as system.")
#FetchContent_Declare(fmt
#    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
#    GIT_TAG 8.1.1
#    UPDATE_DISCONNECTED ON # Only need when following a branch to avoid systematic fetches
#    )
#FetchContent_MakeAvailable(fmt)

if (fmt_POPULATED)
    add_compile_definitions(USE_FMT)
    message(STATUS "Using fmtlib")
else()
    message(STATUS "Using C++ fmt")
endif()

#### GLAD
add_library(glad OBJECT)
target_sources(glad PRIVATE external/glad/src/gl.c)
target_include_directories(glad SYSTEM PUBLIC external/glad/include)

#### GLFW (OpenGL/Win32)
set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "Build the GLFW example programs")
set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "Build the GLFW test programs")
set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "Build the GLFW documentation")
set(GLFW_INSTALL OFF CACHE INTERNAL "Generate installation target")
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
    # UPDATE_DISCONNECTED ON # Only need when following a branch to avoid systematic fetches
    )
FetchContent_MakeAvailable(glfw)
# Prevent GLFW from including the platform's gl.h, we want to use glad's
target_compile_definitions(glfw INTERFACE GLFW_INCLUDE_NONE)

#### glm (matrix/math)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    #GIT_TAG 0.9.9.8
    GIT_TAG 586a402397dd35d66d7a079049856d1e2cbab300
    # UPDATE_DISCONNECTED ON # Only need when following a branch to avoid systematic fetches
    )
FetchContent_MakeAvailable(glm)
# target_compile_definitions(glm INTERFACE
#     GLM_FORCE_SWIZZLE
#     GLM_FORCE_INTRINSICS
#     GLM_CONFIG_SWIZZLE=GLM_SWIZZLE_OPERATOR
#     )


#### stb (image library)
#FetchContent_Declare(stb
#    GIT_REPOSITORY https://github.com/nothings/stb.git
#    UPDATE_DISCONNECTED ON # Only need when following a branch to avoid systematic fetches
#    )
#FetchContent_MakeAvailable(stb)
#add_library(stb OBJECT
#    external/stb/stb_image.cc
#)
#target_include_directories(stb SYSTEM PUBLIC ${stb_SOURCE_DIR})

#### Tracy (profiler)
if (TRACY_ENABLE)
    add_compile_definitions(TRACY_ENABLE)
    FetchContent_Declare(tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG v0.10
        # UPDATE_DISCONNECTED ON # Only need when following a branch to avoid systematic fetches
        )
    # Don't use FetchContent_MakeAvailable because otherwise TracyClient files
    # get installed along our own
    FetchContent_GetProperties(tracy)
    if(NOT tracy_POPULATED)
        FetchContent_Populate(tracy)
        add_subdirectory(${tracy_SOURCE_DIR} ${tracy_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
    target_link_libraries(TracyClient PUBLIC Dbghelp ws2_32)
endif()

#### Dear ImGui (GUI)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/nahor/imgui.git
	GIT_TAG e0c30aa69899a578a4908a9265717d408e73fa19  # v1.90 Docking branch + custom changes
    UPDATE_DISCONNECTED ON # Only need when following a branch to avoid systematic fetches
    )
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp)
target_include_directories(imgui
    SYSTEM PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
target_link_libraries(imgui PUBLIC glfw)
# Breaks ImPlot (v0.16)
# add_compile_definitions(IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
add_compile_definitions(WITH_IMGUI_DOCKING)

#### ImPlot
FetchContent_Declare(implot
    #GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_REPOSITORY https://github.com/nahor/implot.git
    GIT_TAG d0fdb8dc9b0f5fbb27b6cc3447cef390feb7ac8d
    UPDATE_DISCONNECTED ON # Only need when following a branch to avoid systematic fetches
    )
FetchContent_MakeAvailable(implot)
add_library(implot STATIC
    ${implot_SOURCE_DIR}/implot.h
    ${implot_SOURCE_DIR}/implot_internal.h
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
    ${implot_SOURCE_DIR}/implot_demo.cpp
)
target_include_directories(implot
    SYSTEM PUBLIC
        ${implot_SOURCE_DIR}
    )
target_link_libraries(implot PUBLIC imgui)

#### Helper functions

function(copy_resource resource_name)
    configure_file(res/${resource_name} ${CMAKE_BINARY_DIR}/res/${resource_name} COPYONLY)
endfunction()

function(add_data_target data_target)
    # Create the target if it doesn't exist
    if (NOT TARGET ${data_target})
        add_custom_target(${data_target})
    endif()
    # Add the copy command to the target
    foreach(file ${ARGN})
        # CMAKE_CFG_INTDIR is deprecated, but with Visual Studio, it points
        # where the exe will be, unlike CONFIG.
        #set(output_file ${CMAKE_BINARY_DIR}/$<CONFIG>/${file})
        set(output_file ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/${file})
        add_custom_command(
            OUTPUT ${output_file}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/${file}
                            ${output_file}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${file}
            VERBATIM)
        target_sources(${data_target} PRIVATE ${output_file})
    endforeach()
endfunction()

#### Main application
add_executable(sensor
    src/main.cc
    src/window.cc
    src/ui/imgui_env.cc
    #src/ui/tonemap.cc
    #src/engine/compute_program.cc
    #src/engine/framebuffer.cc
    #src/engine/renderer.cc
    #src/engine/shader_program.cc
    #src/engine/shader_unit.cc
    #src/engine/texture.cc
    #src/engine/texture_cache.cc
    #src/game/bloom.cc
    #src/game/bullet.cc
    #src/game/line_renderer.cc
    #src/game/swarm.cc
    #src/game/terrain.cc
    #src/game/world_info.cc
    #src/utils/misc.cc
    #src/utils/thread_pool.cc
    src/utils/print.h
    )
target_sources(sensor PUBLIC ${RESOURCE_FILES})
target_include_directories(sensor PRIVATE src)
if(MSVC)
    target_compile_options(sensor PRIVATE -W4)
else()
    target_compile_options(sensor PRIVATE -Wall -Wextra)
endif()
# Put stb before glfw, since glfw comes with its own stb headers
#target_link_libraries(sensor PRIVATE implot imgui stb glfw glad glm phmap)
target_link_libraries(sensor PRIVATE implot imgui glfw glad glm)
if (NOT MSVC)
    target_link_libraries(sensor PRIVATE atomic)
endif()
if (fmt_POPULATED)
    target_link_libraries(sensor PRIVATE fmt::fmt)
endif()
if (TRACY_ENABLE)
    target_link_libraries(sensor PRIVATE Tracy::TracyClient)
endif()
#target_link_libraries(sensor PRIVATE -static)

set(RESOURCE_FILES
    #res/...
    )
add_data_target(sensor_data ${RESOURCE_FILES})
add_dependencies(sensor sensor_data)

if(WIN32)
#    find_program(windres
#        NAMES rc.exe windres.exe x86_64-w64-mingw32-windres.exe llvm-windres.exe
#        REQUIRED
#        )
#    if(windres)
#        message(STATUS "Found windres... ${windres}")
#    endif()
#    add_custom_command(
#        OUTPUT "${CMAKE_BINARY_DIR}/CMakeFiles/sensor_$<CONFIG>.rc.o"
#        COMMENT "Generating resource file"
#        COMMAND ${windres}
#            -I ${CMAKE_CURRENT_SOURCE_DIR}
#            --input ${CMAKE_CURRENT_SOURCE_DIR}/sensor.rc
#            --output "${CMAKE_BINARY_DIR}/CMakeFiles/sensor_$<CONFIG>.rc.o"
#            --output-format=coff
#        MAIN_DEPENDENCY ${CMAKE_CURRENT_SOURCE_DIR}/sensor.rc
#        DEPENDS
#            ${CMAKE_CURRENT_SOURCE_DIR}/sensor.exe.manifest
#            ${CMAKE_CURRENT_SOURCE_DIR}/res/Icon.ico
#        VERBATIM)
#    target_sources(sensor PRIVATE "${CMAKE_BINARY_DIR}/CMakeFiles/sensor_$<CONFIG>.rc.o")
target_sources(sensor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/sensor.rc" "${CMAKE_CURRENT_SOURCE_DIR}/sensor.exe.manifest")
endif()

include(GNUInstallDirs)
install(
    TARGETS sensor
    RUNTIME DESTINATION "."
        PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE
    )
install(
    DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>/res
    DESTINATION "."
    REGEX '.*'
        PERMISSIONS
            OWNER_READ OWNER_WRITE
            GROUP_READ
            WORLD_READ
    )

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
set(CPACK_SOURCE_GENERATOR "ZIP")
include(CPack)
