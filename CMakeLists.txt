cmake_minimum_required(VERSION 3.0.0)
project(HSHRServer)

add_definitions("-Wall")
set(CMAKE_CXX_STANDARD 14)

include_directories(${PROJECT_SOURCE_DIR}/include)
add_subdirectory(http)

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} http pthread)