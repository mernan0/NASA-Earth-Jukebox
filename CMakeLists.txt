cmake_minimum_required(VERSION 4.0)
project(NASA_Earth_Jukebox)

set(CMAKE_CXX_STANDARD 20)

set(BUILD_SHARED_LIBS OFF)

# Automatically download and build SFML 2.6.1
include(FetchContent)
FetchContent_Declare(SFML
        GIT_REPOSITORY https://github.com/SFML/SFML.git
        GIT_TAG 2.6.1
)
FetchContent_MakeAvailable(SFML)

add_executable(NASA_Earth_Jukebox main.cpp)

# Link the required SFML modules
target_link_libraries(NASA_Earth_Jukebox PRIVATE sfml-graphics sfml-window sfml-system)