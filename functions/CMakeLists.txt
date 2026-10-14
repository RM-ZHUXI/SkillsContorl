set(SOURCES
    calibrate_camera.cpp
)


add_library(functions SHARED ${SOURCES})

target_include_directories(functions
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(functions
    PUBLIC
        fmt::fmt
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        yaml-cpp::yaml-cpp
        tools
)