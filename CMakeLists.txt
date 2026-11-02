cmake_minimum_required(VERSION 3.10)

project(CAMERA_TEST_SKILL VERSION 1.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(SKILLCONTROL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

find_package(OpenCV REQUIRED)
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(yaml-cpp REQUIRED)

if(NOT TARGET yaml-cpp::yaml-cpp)
    if(TARGET yaml-cpp)
        add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
    else()
        message(FATAL_ERROR "yaml-cpp target not found")
    endif()
endif()

set(SKILLCONTROL_TOOLS_LIB "" CACHE FILEPATH "手动指定 libtools 动态库路径（留空则自动探测）")

set(_tools_candidates
    "${SKILLCONTROL_TOOLS_LIB}"
    "${SKILLCONTROL_ROOT}/libtools.dll"           # 发布布局 Windows
    "${SKILLCONTROL_ROOT}/libtools.so"            # 发布布局 Linux
    "${SKILLCONTROL_ROOT}/build/bin/libtools.dll" # 源码树 Windows
    "${SKILLCONTROL_ROOT}/build/lib/libtools.so"  # 源码树 Linux
    "${SKILLCONTROL_ROOT}/build/lib/libtools.dll" # 源码树（有人手工拷过）
)

set(TOOLS_LIB "")
foreach(_cand IN LISTS _tools_candidates)
    if(_cand AND EXISTS "${_cand}")
        set(TOOLS_LIB "${_cand}")
        break()
    endif()
endforeach()

if(NOT TOOLS_LIB)
    message(FATAL_ERROR
        "找不到框架库 libtools，请先在顶层工程里构建 tools（cmake --build build），"
        "或用 -DSKILLCONTROL_TOOLS_LIB=<路径> 指定。已尝试:\n  "
        "${_tools_candidates}")
endif()
message(STATUS "camera_test: libtools = ${TOOLS_LIB}")

set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/display.cpp
)

add_library(CAMERA_TEST SHARED ${SOURCES})

target_include_directories(CAMERA_TEST
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include   # 技能自己的 include
        ${SKILLCONTROL_ROOT}/include          # 主工程 include（skillscontrol.hpp / tools/）
        ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(CAMERA_TEST
    PUBLIC
        ${TOOLS_LIB}
        ${OpenCV_LIBS}
        fmt::fmt
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        yaml-cpp::yaml-cpp
        ${CMAKE_DL_LIBS}      # Linux: dladdr —— 用本 .so 自己的位置定位 config.yaml
)

# ============================================================
# rpath：运行时能找到框架库
# ============================================================
get_filename_component(TOOLS_LIB_DIR "${TOOLS_LIB}" DIRECTORY)

set_target_properties(CAMERA_TEST PROPERTIES
    BUILD_RPATH   "${TOOLS_LIB_DIR};${SKILLCONTROL_ROOT}"
    INSTALL_RPATH "${TOOLS_LIB_DIR};${SKILLCONTROL_ROOT}"
)
