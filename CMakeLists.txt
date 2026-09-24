cmake_minimum_required(VERSION 3.18)
project(qmmp-spotify VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Network NetworkAuth Widgets Test)

enable_testing()
add_subdirectory(src/logic)
add_subdirectory(tests)
