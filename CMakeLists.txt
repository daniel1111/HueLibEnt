cmake_minimum_required(VERSION 3.7.2)

project(HueEnt VERSION 0.0.1 LANGUAGES C)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_library(HueEnt src/hue_entertainment.c src/hue_rest.c src/hue_dtls.c)

# CURL
find_package(CURL REQUIRED)
include_directories(${CURL_INCLUDE_DIR})
target_link_libraries(HueEnt ${CURL_LIBRARIES})

# OpenSSL
find_package(OpenSSL REQUIRED)
target_link_libraries(HueEnt OpenSSL::SSL)

# json-c
target_link_libraries(HueEnt json-c)


target_include_directories(HueEnt
    PUBLIC 
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

OPTION(EXAMPLE_BASIC_COLOUR_FADE "Build BasicColourFade example" ON)
OPTION(EXAMPLE_HDMX "Build Hdmx example" ON)
OPTION(EXAMPLE_HUEVIS "Build HueVis example" ON)
OPTION(EXAMPLE_HUTIL "Build Hutil example" ON)

# Example: BasicColourFade
IF(EXAMPLE_BASIC_COLOUR_FADE)
    add_executable(bcf examples/BasicColourFade/main.c)
    target_link_libraries(bcf PUBLIC HueEnt)
ENDIF(EXAMPLE_BASIC_COLOUR_FADE)

# Example: Hdmx
IF(EXAMPLE_HDMX)
    add_executable(hdmx examples/Hdmx/main.c)
    target_link_libraries(hdmx PUBLIC HueEnt)
ENDIF(EXAMPLE_HDMX)

# Example: HueVis
IF(EXAMPLE_HUEVIS)
    IF(NOT CMAKE_HOST_APPLE)
    add_executable(huevis examples/HueVis/huevis.c examples/HueVis/input_pulse.c examples/HueVis/input_squeezelite.c examples/HueVis/process_cava.c)
    target_link_libraries(huevis PUBLIC HueEnt)
    find_package (Threads)
    target_link_libraries(huevis PUBLIC ${CMAKE_THREAD_LIBS_INIT})
    target_link_libraries(huevis PUBLIC fftw3)
    target_link_libraries(huevis PUBLIC m)
    target_link_libraries(huevis PUBLIC rt)
    target_link_libraries(huevis PUBLIC config)
    target_link_libraries(huevis PUBLIC pulse)
    target_link_libraries(huevis PUBLIC pulse-simple)
    ENDIF(NOT CMAKE_HOST_APPLE)
ENDIF(EXAMPLE_HUEVIS)

# Example: Hutil
IF(EXAMPLE_HUTIL)
    add_executable(hutil examples/Hutil/main.c)
    target_link_libraries(hutil PUBLIC HueEnt)
    target_link_libraries(hutil PUBLIC config)
ENDIF(EXAMPLE_HUTIL)


