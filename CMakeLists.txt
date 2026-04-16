cmake_minimum_required(VERSION 3.10)
project(distributed_coordinator)

set(CMAKE_CXX_STANDARD 11)

include_directories(include)

add_executable(worker
        src/worker/main.cpp
        src/worker/worker.cpp
        src/net/net_utils.cpp
        src/rpc/client.cpp
        src/rpc/server_connection.cpp
        src/coordinator/coordinator.cpp
        src/router/router.cpp
        src/client/client.cpp
        src/client/main.cpp
        src/coordinator/main.cpp
        src/router/main.cpp
        src/top/top.cpp
        src/top/main.cpp
)