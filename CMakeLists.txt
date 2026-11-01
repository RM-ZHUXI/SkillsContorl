cmake_minimum_required(VERSION 3.10)

project(GIMBAL_SKILL VERSION 1.0.0 LANGUAGES CXX)

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

# ============================================================
# 依赖包
# ============================================================
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

# ============================================================
# 定位框架库 libtools (仅针对 Linux)
# ============================================================
set(SKILLCONTROL_TOOLS_LIB "" CACHE FILEPATH "手动指定 libtools 动态库路径（留空则自动探测）")

set(_tools_candidates "${SKILLCONTROL_TOOLS_LIB}")
list(APPEND _tools_candidates
    "${SKILLCONTROL_ROOT}/libtools.so"              # 发布布局
    "${SKILLCONTROL_ROOT}/build/bin/libtools.so"    # 源码树 bin/
    "${SKILLCONTROL_ROOT}/build/lib/libtools.so"    # 源码树 lib/
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
        "找不到框架库 libtools.so，请先在顶层工程里构建 tools（cmake --build build），"
        "或用 -DSKILLCONTROL_TOOLS_LIB=<路径> 指定。已尝试:\n  "
        "${_tools_candidates}")
endif()
message(STATUS "GIMBAL_SKILL: 找到 libtools = ${TOOLS_LIB}")

# ============================================================
# 目标 (改名为 GIMBAL_SKILL)
# ============================================================
set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gimbal.cpp
)

add_library(GIMBAL_SKILL SHARED ${SOURCES})

target_include_directories(GIMBAL_SKILL
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include   # 技能自己的 include
        ${SKILLCONTROL_ROOT}/include          # 主工程 include（skillscontrol.hpp / tools/）
        ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(GIMBAL_SKILL
    PUBLIC
        ${TOOLS_LIB}
        ${OpenCV_LIBS}
        fmt::fmt
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        yaml-cpp::yaml-cpp
        ${CMAKE_DL_LIBS}     
)

set_target_properties(GIMBAL_SKILL PROPERTIES
    BUILD_RPATH   "${MVS_LIB_DIR};${TOOLS_LIB_DIR};${SKILLCONTROL_ROOT}"
    INSTALL_RPATH "${MVS_LIB_DIR};${TOOLS_LIB_DIR};${SKILLCONTROL_ROOT}"
)