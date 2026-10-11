cmake_minimum_required(VERSION 3.10)

project(HAIKANG_CAMERA_SKILL VERSION 1.0.5 LANGUAGES CXX)

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
#   "build\\bin\\libHAIKANG_CAMERA"
# 由宿主的 LoadSkills 按平台补 ".dll" / ".so"（见 tools/loadskills.cpp 的 LIB_EXTENSION），
# 于是一份 config.json 在 Windows 和 Linux 上都能定位到产物，不用来回改。
# lib/ 只留给导入库 / 静态库（Windows 的 .dll.a）。
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
# 海康 MVS SDK
# ============================================================
# 旧版本这里只写了一句 link_directories(${MVS_LIB_DIR})，但这个变量从来没被定义过，
# 展开就是空的；rpath 里的 ${MVS_LIB_DIR} 同样为空。现在统一定义清楚：
#   MVS_ROOT      : SDK 根目录（只用来取头文件，机器相关所以做成 CACHE 可覆盖）
#   MVS_DIR       : 本技能自带的 MVS/ 目录，MinGW 用的导入库和运行时 DLL 都在这里
#   MVS_IMPORT_LIB: 链接时用的导入库
set(MVS_ROOT "E:/APP/MVS/Development" CACHE PATH "海康 MVS SDK 根目录（取 Includes）")
set(MVS_INCLUDE_DIR "${MVS_ROOT}/Includes")
set(MVS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/MVS")

if(WIN32)
    set(MVS_IMPORT_LIB "${MVS_DIR}/libMvCameraControl.a")
else()
    # Linux 上 MVS 提供的是 libMvCameraControl.so，装完通常在 /opt/MVS/lib/64
    set(MVS_IMPORT_LIB "")
    foreach(_cand
            "${MVS_DIR}/libMvCameraControl.so"
            "${MVS_ROOT}/Libraries/linux64/libMvCameraControl.so"
            "/opt/MVS/lib/64/libMvCameraControl.so")
        if(EXISTS "${_cand}")
            set(MVS_IMPORT_LIB "${_cand}")
            break()
        endif()
    endforeach()
endif()

if(NOT EXISTS "${MVS_INCLUDE_DIR}/MvCameraControl.h")
    message(FATAL_ERROR
        "找不到海康 MVS 头文件: ${MVS_INCLUDE_DIR}/MvCameraControl.h\n"
        "请用 -DMVS_ROOT=<MVS SDK 根目录> 指定（该目录下应当有 Includes/）。")
endif()

if(NOT MVS_IMPORT_LIB OR NOT EXISTS "${MVS_IMPORT_LIB}")
    message(FATAL_ERROR
        "找不到 MVS 导入库: '${MVS_IMPORT_LIB}'\n"
        "Windows(MinGW) 需要 ${MVS_DIR}/libMvCameraControl.a；\n"
        "Linux 需要 libMvCameraControl.so（可用 -DMVS_ROOT=... 指定 SDK 位置）。")
endif()

# ============================================================
# 定位框架库 libtools
# ============================================================
# 三种布局都要支持：
#   发布布局  <release>/win/skills/<技能>/    → <release>/win/libtools.dll
#             <release>/linux/skills/<技能>/  → <release>/linux/libtools.so
#   源码树    <repo>/skills/<技能>/           → <repo>/build/bin/libtools.dll  (Windows 运行时产物)
#                                              <repo>/build/lib/libtools.so    (Linux 运行时产物)
# @note 旧版本写死 ${CMAKE_SOURCE_DIR}/../../libtools.dll：
#       它在发布布局下成立，但在源码树里指向 <repo>/libtools.dll —— 那个文件不存在，
#       而且这里没有兜底，所以在源码树里根本 configure/link 不过去。
#       想要人为指定时用 -DSKILLCONTROL_TOOLS_LIB=<路径>。
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
message(STATUS "haikang_camera: libtools = ${TOOLS_LIB}")

# ============================================================
# 目标
# ============================================================
set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/camera.cpp
)

add_library(HAIKANG_CAMERA SHARED ${SOURCES})

target_include_directories(HAIKANG_CAMERA
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include   # 技能自己的 include
        ${SKILLCONTROL_ROOT}/include          # 主工程 include（skillscontrol.hpp / tools/）
        ${OpenCV_INCLUDE_DIRS}
        ${MVS_INCLUDE_DIR}
)

# 海康 SDK 是 MSVC 编的，MinGW 下静态链接 gcc/stdc++ 运行时少一层 DLL 依赖
if(MINGW)
    target_link_options(HAIKANG_CAMERA PRIVATE
        -static-libgcc
        -static-libstdc++
    )
endif()

target_link_libraries(HAIKANG_CAMERA
    PUBLIC
        ${TOOLS_LIB}
        ${MVS_IMPORT_LIB}
        ${OpenCV_LIBS}
        fmt::fmt
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        yaml-cpp::yaml-cpp
        ${CMAKE_DL_LIBS}      # Linux: dladdr —— 用本 .so 自己的位置定位 config.yaml
)

# ============================================================
# rpath：运行时能找到框架库和海康运行时 DLL
# ============================================================
# 直接指向"实际找到那个文件"所在目录，源码树和发布布局都能覆盖。
# Windows 下 rpath 被忽略（靠 PATH 和 exe 同目录），无副作用。
get_filename_component(TOOLS_LIB_DIR "${TOOLS_LIB}" DIRECTORY)

set_target_properties(HAIKANG_CAMERA PROPERTIES
    BUILD_RPATH   "${MVS_DIR};${TOOLS_LIB_DIR};${SKILLCONTROL_ROOT}"
    INSTALL_RPATH "${MVS_DIR};${TOOLS_LIB_DIR};${SKILLCONTROL_ROOT}"
)
