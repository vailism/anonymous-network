cmake_minimum_required(VERSION 3.16)
project(anonymous_network LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(common STATIC
    common/src/config.cpp
    common/src/crypto.cpp
    common/src/logger.cpp
    common/src/protocol.cpp
    common/src/socket_utils.cpp
)

target_include_directories(common PUBLIC common/include)

add_executable(anon_client
    client/src/client_app.cpp
    client/src/main.cpp
)
target_include_directories(anon_client PRIVATE client/include)
target_link_libraries(anon_client PRIVATE common Threads::Threads)

add_executable(anon_node
    node/src/main.cpp
    node/src/relay_node.cpp
)
target_include_directories(anon_node PRIVATE node/include)
target_link_libraries(anon_node PRIVATE common Threads::Threads)

add_executable(anon_server
    server/src/final_server.cpp
    server/src/main.cpp
)
target_include_directories(anon_server PRIVATE server/include)
target_link_libraries(anon_server PRIVATE common Threads::Threads)
