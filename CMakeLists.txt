cmake_minimum_required(VERSION 3.7.2)

project(HueEnt VERSION 0.0.1 LANGUAGES C)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_library(HueEnt src/hue_entertainment.c src/hue_rest.c src/dtls.c)

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

# Example: BasicColourFade
add_executable(bcf examples/BasicColourFade/main.c)
target_link_libraries(bcf PUBLIC HueEnt)

# Example: Hdmx
add_executable(hdmx examples/Hdmx/main.c)
target_link_libraries(hdmx PUBLIC HueEnt)

# Example: HueVis
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

# Example: Hutil
add_executable(hutil examples/Hutil/main.c)
target_link_libraries(hutil PUBLIC HueEnt)
target_link_libraries(hutil PUBLIC config)



