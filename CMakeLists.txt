cmake_minimum_required(VERSION 3.15.3)
project(mpc-cli)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_search_module(GSTREAMER REQUIRED gstreamer-1.0>=1.4)
pkg_search_module(GSTREAMER_APP REQUIRED gstreamer-app-1.0>=1.4)
pkg_search_module(YAMLCPP REQUIRED yaml-cpp)

# Include directories
include_directories(
  ${GSTREAMER_INCLUDE_DIRS}
  ${GSTREAMER_APP_INCLUDE_DIRS}
  ${YAMLCPP_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/src
)

# Link directories
link_directories(
  ${GSTREAMER_LIBRARY_DIRS}
  ${GSTREAMER_APP_LIBRARY_DIRS}
  ${YAMLCPP_LIBRARY_DIRS}
)

# Source files
set(SOURCES
  src/main.cpp
  src/gstreamer/gst_pipeline.cpp
  src/audio-processor/audio_processor.cpp
  src/input/keyboard_input.mm
  src/visualizer/wave_visualizer.cpp
  src/sequencer/sequencer.cpp
)

# Create executable
add_executable(mpc-cli ${SOURCES})

# Link libraries
target_link_libraries(mpc-cli
  ${GSTREAMER_LIBRARIES}
  ${GSTREAMER_APP_LIBRARIES}
  ${YAMLCPP_LIBRARIES}
  "-framework CoreFoundation"
  "-framework Carbon"
)

# Compiler flags
target_compile_options(mpc-cli PRIVATE
  ${GSTREAMER_CFLAGS_OTHER}
  ${GSTREAMER_APP_CFLAGS_OTHER}
)
