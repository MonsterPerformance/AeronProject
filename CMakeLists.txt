cmake_minimum_required(VERSION 3.20)
project(aeron_project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(LockFreeQueue REQUIRED)
find_package(Aeron REQUIRED)

add_executable(aeron_subscriber src/aeron_subscriber.cpp)
target_link_libraries(aeron_subscriber
    PRIVATE
        LockFreeQueue::LockFreeQueue 
        Aeron::AeronGateway)

add_executable(aeron_publisher src/aeron_publisher.cpp)
target_link_libraries(aeron_publisher
    PRIVATE
        Aeron::AeronGateway)
