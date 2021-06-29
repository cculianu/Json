cmake_minimum_required(VERSION 3.1.0)

project(Json VERSION 1.3.4 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif()

find_package(Qt5 COMPONENTS Core REQUIRED)

add_executable(Json
    src/main.cpp
    src/Json.cpp
    src/Json_Parser.cpp
)

target_link_libraries(Json Qt5::Core)
