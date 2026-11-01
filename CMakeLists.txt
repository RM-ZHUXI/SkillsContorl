cmake_minimum_required(VERSION 3.10)

project(skillcontorl VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 查找依赖包
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

# 添加子目录
add_subdirectory(tools)
add_subdirectory(net)
add_subdirectory(functions)

# 创建主程序
add_executable(skillcontorl projects/main.cpp)

# 包含路径
target_include_directories(skillcontorl
    PRIVATE
        ${OpenCV_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/include
)

# 链接库
target_link_libraries(skillcontorl
    PRIVATE
        tools                
        net
        functions
        ${OpenCV_LIBS}      
)

# 崩溃时能打印函数名的调用栈（libtools 里的致命信号处理用到 backtrace_symbols_fd）
# 没有 -rdynamic 的话栈里只有地址，需要自己 addr2line；Linux 下建议保留
if(UNIX AND NOT APPLE)
    target_link_options(skillcontorl PRIVATE -rdynamic)
endif()