cmake_minimum_required(VERSION 3.10)
project(distributed_coordinator)

set(CMAKE_CXX_STANDARD 11)

include_directories(src)

add_executable(worker
        worker/main.cpp
        worker/worker.cpp
        net/net_utils.cpp
        rpc/client.cpp
        rpc/server_connection.cpp
        coordinator/coordinator.cpp
)