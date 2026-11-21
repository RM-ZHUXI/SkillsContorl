set(SOURCES
    memory.cpp
)


add_library(group SHARED ${SOURCES})

target_include_directories(group
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(group
    PUBLIC
        functions  
        fmt::fmt
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        yaml-cpp::yaml-cpp
        tools
        functions
)

if(WIN32)
    target_compile_definitions(group PRIVATE TOOLS_EXPORTS)
endif()