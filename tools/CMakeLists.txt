set(SOURCES
    logger.cpp
    time.cpp
    findskills.cpp
    loadskills.cpp
    runskillslists.cpp
    memory.cpp
    type.cpp
    assign.cpp
    runskills.cpp
)


add_library(tools SHARED ${SOURCES})

target_include_directories(tools
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(tools
    PUBLIC
        fmt::fmt
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        yaml-cpp::yaml-cpp
        ${OpenCV_LIBS}   
)

if(WIN32)
    target_compile_definitions(tools PRIVATE TOOLS_EXPORTS)
endif()
