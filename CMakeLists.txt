cmake_minimum_required(VERSION 3.10)

project(PREDICT_SKILL VERSION 1.4.0 LANGUAGES CXX)

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
find_package(Eigen3 REQUIRED NO_MODULE)
find_package(OpenVINO REQUIRED)

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
    "${SKILLCONTROL_ROOT}/build/lib/libtools.dll" # 源码树
)

set(TOOLS_LIB "")
foreach(_cand IN LISTS _tools_candidates)
    if(_cand AND EXISTS "${_cand}")
        set(TOOLS_LIB "${_cand}")
        break()
    endif()
endforeach()

set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/solver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/armor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/math_tools.cpp
)

add_library(PREDICT SHARED ${SOURCES})

target_include_directories(PREDICT
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include  
        ${SKILLCONTROL_ROOT}/include         
        ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(PREDICT
    PUBLIC
        ${TOOLS_LIB}
        ${OpenCV_LIBS}
        fmt::fmt
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        yaml-cpp::yaml-cpp
        ${CMAKE_DL_LIBS}  
        Eigen3::Eigen
        openvino::runtime
)

# ============================================================
# rpath：运行时能找到框架库
# ============================================================
get_filename_component(TOOLS_LIB_DIR "${TOOLS_LIB}" DIRECTORY)

set_target_properties(PREDICT PROPERTIES
    BUILD_RPATH   "${TOOLS_LIB_DIR};${SKILLCONTROL_ROOT}"
    INSTALL_RPATH "${TOOLS_LIB_DIR};${SKILLCONTROL_ROOT}"
)

