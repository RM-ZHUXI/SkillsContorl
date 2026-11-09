cmake_minimum_required(VERSION 3.10)

project(CAMERA_TEST_SKILL VERSION 1.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 单独 configure 时默认 Release（和顶层工程保持一致）；用 -DCMAKE_BUILD_TYPE=Debug 可覆盖
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ============================================================
# 目录
# ============================================================
# 顶层工程根目录：<repo> 或发布包根 <release>/<ver>/<os>
# @note 一律用 CMAKE_CURRENT_SOURCE_DIR 而不是 CMAKE_SOURCE_DIR：
#       本文件被 add_subdirectory() 引入时 CMAKE_SOURCE_DIR 会变成顶层目录，
#       用 CMAKE_SOURCE_DIR 会让 include 路径整个错位。
set(SKILLCONTROL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# 输出目录（要和 config.json 里的 url 对上）
#   Windows : SHARED 库属于 RUNTIME 产物 → 本来就是 build/bin
#   Linux   : SHARED 库默认属于 LIBRARY 产物 → 默认会落到 build/lib
# 这里把两边都统一到 build/bin，好处是 config.json 里的 url 可以写成**不带扩展名**的
#   "build\\bin\\libCAMERA_TEST"
# 由宿主的 LoadSkills 按平台补 ".dll" / ".so"（见 tools/loadskills.cpp 的 LIB_EXTENSION），
# 于是一份 config.json 在 Windows 和 Linux 上都能定位到产物，不用来回改。
# lib/ 只留给导入库 / 静态库（Windows 的 .dll.a）。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ============================================================
# 依赖包（本技能不需要 MVS SDK，只要 OpenCV + yaml-cpp + 框架）
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
# 定位框架库 libtools
# ============================================================
# 三种布局都要支持：
#   发布布局  <release>/win/skills/<技能>/    → <release>/win/libtools.dll
#             <release>/linux/skills/<技能>/  → <release>/linux/libtools.so
#   源码树    <repo>/skills/<技能>/           → <repo>/build/bin/libtools.dll  (Windows 运行时产物)
#                                              <repo>/build/lib/libtools.so    (Linux 运行时产物)
# @note 旧版本写死 ${CMAKE_SOURCE_DIR}/../../libtools.dll，兜底是
#       ../../build/lib/libtools.dll —— 这两个在源码树里都不存在
#       （MinGW 生成 SHARED 库时产物落在 build/bin，build/lib 里只有 .dll.a 导入库），
#       所以源码树里 configure/link 不过去。想要人为指定时用 -DSKILLCONTROL_TOOLS_LIB=<路径>。
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

# ============================================================
# 目标
# ============================================================
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
# 直接指向"实际找到那个文件"所在目录，源码树和发布布局都能覆盖。
# Windows 下 rpath 被忽略（靠 PATH 和 exe 同目录），无副作用。
get_filename_component(TOOLS_LIB_DIR "${TOOLS_LIB}" DIRECTORY)

set_target_properties(CAMERA_TEST PROPERTIES
    BUILD_RPATH   "${TOOLS_LIB_DIR};${SKILLCONTROL_ROOT}"
    INSTALL_RPATH "${TOOLS_LIB_DIR};${SKILLCONTROL_ROOT}"
)
