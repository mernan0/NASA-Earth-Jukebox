cmake_minimum_required(VERSION 3.22)
project(NASA_Earth_Jukebox LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_SHARED_LIBS OFF)

# Automatically download and build SFML 2.6.1 from GitHub
include(FetchContent)
FetchContent_Declare(SFML
        GIT_REPOSITORY https://github.com/SFML/SFML.git
        GIT_TAG 2.6.1
)
FetchContent_MakeAvailable(SFML)

add_executable(NASA_Earth_Jukebox main.cpp)

# Link all required SFML modules (including Audio)
target_link_libraries(NASA_Earth_Jukebox PRIVATE
        sfml-graphics
        sfml-audio
        sfml-window
        sfml-system
)

# Automatically copy the 'Data' folder next to the executable
add_custom_command(TARGET NASA_Earth_Jukebox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/Data"
        "$<TARGET_FILE_DIR:NASA_Earth_Jukebox>/Data"
)

# Automatically copy openal32.dll required by sfml-audio on Windows
if(WIN32)
    add_custom_command(TARGET NASA_Earth_Jukebox POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${sfml_SOURCE_DIR}/extlibs/bin/x64/openal32.dll"
            "$<TARGET_FILE_DIR:NASA_Earth_Jukebox>"
    )
endif()