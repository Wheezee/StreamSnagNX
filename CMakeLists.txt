cmake_minimum_required(VERSION 3.18)

if (NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR "DEVKITPRO is not set. Install devkitPro and export DEVKITPRO before building.")
endif ()

set(DEVKITPRO $ENV{DEVKITPRO})
set(CMAKE_TOOLCHAIN_FILE ${DEVKITPRO}/cmake/Switch.cmake CACHE FILEPATH "devkitPro Switch toolchain")

project(StreamSnagNX LANGUAGES C CXX)

set(SSNX_VERSION "0.1.0" CACHE STRING "StreamSnagNX application version")

set(PLATFORM_SWITCH ON)
set(USE_GLFW OFF CACHE BOOL "Disable GLFW for Deko3D" FORCE)
set(USE_DEKO3D ON CACHE BOOL "Use Deko3D on Switch" FORCE)
set(BOREALIS_USE_DEKO3D ON CACHE BOOL "Use Borealis Deko3D driver" FORCE)
set(USE_GLES2 OFF CACHE BOOL "Disable GLES for Deko3D" FORCE)
set(USE_SDL2 OFF)
set(USE_GLES3 OFF CACHE BOOL "Disable OpenGL ES 3 on Switch")
set(BRLS_UNITY_BUILD OFF CACHE BOOL "Disable Borealis unity builds")
set(FMT_OS OFF CACHE BOOL "Disable fmt OS-specific APIs for devkitA64")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(JANSSON REQUIRED IMPORTED_TARGET jansson)
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(SWRESAMPLE REQUIRED IMPORTED_TARGET libswresample)

# Sprint 1: reuse the vendored Borealis from the OpenNOW checkout.
# TODO: vendor our own copy (git submodule) before any release.
set(OPENNOW_BOREALIS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../OpenNOW-Switch/extern/borealis/library)
if (NOT EXISTS ${OPENNOW_BOREALIS_DIR}/CMakeLists.txt)
    message(FATAL_ERROR "Borealis not found at ${OPENNOW_BOREALIS_DIR}")
endif ()
add_subdirectory(${OPENNOW_BOREALIS_DIR} borealis)

target_include_directories(borealis SYSTEM AFTER PUBLIC ${DEVKITPRO}/portlibs/switch/include)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    app/src/*.cpp
    app/src/*.c
)

# QuickJS engine for n-param throttling solver
set(QJS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_CLI OFF CACHE BOOL "" FORCE)
if (PLATFORM_SWITCH)
    set(CMAKE_DL_LIBS "" CACHE STRING "" FORCE)
    set(CMAKE_THREAD_LIBS_INIT "" CACHE STRING "" FORCE)
    set(M_LIBRARIES "" CACHE STRING "" FORCE)
    set(M_LIBRARIES-NOTFOUND TRUE CACHE BOOL "" FORCE)
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/vendor/quickjs EXCLUDE_FROM_ALL)

# Borealis Switch init wrapper: provides userAppInit() which calls
# romfsInit() + service inits. Without it, romfs:/ is never mounted
# (ENODEV) and sockets/services stay down. Mirrors OpenNOW.
list(APPEND APP_SOURCES
    ${OPENNOW_BOREALIS_DIR}/lib/platforms/switch/switch_wrapper.c
)

add_executable(${PROJECT_NAME} ${APP_SOURCES})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        app/src
        vendor/quickjs
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    PLATFORM_SWITCH
    __SWITCH__
    HAS_SOCKLEN_T=1
    SSNX_APP_VERSION="${SSNX_VERSION}"
    USE_DK_RENDERER
    BOREALIS_USE_DEKO3D
)

# Borealis Deko3D driver shaders (fill only for sprint 1; no video yet).
if (PLATFORM_SWITCH)
    find_program(NX_UAM_EXE NAMES uam HINTS "${DEVKITPRO}/tools/bin")
    if (NOT NX_UAM_EXE)
        message(FATAL_ERROR "uam is required for the Borealis Deko3D shaders")
    endif ()
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/resources/shaders")
    set(BOREALIS_DEKO_SHADER_DIR
        "${OPENNOW_BOREALIS_DIR}/lib/extern/nanovg/deko3d/shaders")
    execute_process(COMMAND ${NX_UAM_EXE} -s vert
        -o "${CMAKE_BINARY_DIR}/resources/shaders/fill_vsh.dksh"
        "${BOREALIS_DEKO_SHADER_DIR}/fill_vsh.glsl"
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND ${NX_UAM_EXE} -s frag
        -o "${CMAKE_BINARY_DIR}/resources/shaders/fill_aa_fsh.dksh"
        "${BOREALIS_DEKO_SHADER_DIR}/fill_aa_fsh.glsl"
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND ${NX_UAM_EXE} -s frag
        -o "${CMAKE_BINARY_DIR}/resources/shaders/fill_fsh.dksh"
        "${BOREALIS_DEKO_SHADER_DIR}/fill_fsh.glsl"
        COMMAND_ERROR_IS_FATAL ANY)
endif ()

target_compile_options(${PROJECT_NAME}
    PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        -ffunction-sections
        -fdata-sections
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        borealis
        qjs
        PkgConfig::CURL
        PkgConfig::JANSSON
        PkgConfig::AVFORMAT
        PkgConfig::AVCODEC
        PkgConfig::AVUTIL
        PkgConfig::SWRESAMPLE
)

target_compile_definitions(${PROJECT_NAME} PRIVATE BRLS_RESOURCES="romfs:/")

set(PROJECT_AUTHOR "Wheezee")
set(PROJECT_VERSION_STRING "${SSNX_VERSION}")
set(PROJECT_TITLEID "05534E41474E5858")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.jpg)
set(PROJECT_ROMFS_DIR ${CMAKE_BINARY_DIR}/romfs)

add_custom_target(${PROJECT_NAME}.nro
    DEPENDS ${PROJECT_NAME}
    COMMAND ${NX_NACPTOOL_EXE}
        --create
        "StreamSnagNX"
        "${PROJECT_AUTHOR}"
        "${PROJECT_VERSION_STRING}"
        ${PROJECT_NAME}.nacp
        --titleid=${PROJECT_TITLEID}
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${PROJECT_ROMFS_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_ROMFS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/font
        ${PROJECT_ROMFS_DIR}/font
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n
        ${PROJECT_ROMFS_DIR}/i18n
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/img
        ${PROJECT_ROMFS_DIR}/img
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/material
        ${PROJECT_ROMFS_DIR}/material
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/resources/shaders
        ${PROJECT_ROMFS_DIR}/shaders
    COMMAND ${NX_ELF2NRO_EXE}
        ${PROJECT_NAME}.elf
        ${PROJECT_NAME}.nro
        --icon=${PROJECT_ICON}
        --nacp=${PROJECT_NAME}.nacp
        --romfsdir=${PROJECT_ROMFS_DIR}
)
