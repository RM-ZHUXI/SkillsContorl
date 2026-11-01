set(SOURCES
    http.cpp
    http_server.cpp
)


add_library(net SHARED ${SOURCES})

target_include_directories(net
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(net
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
    target_link_libraries(net PRIVATE ws2_32)
endif()
