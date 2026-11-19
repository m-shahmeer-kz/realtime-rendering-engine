cmake_minimum_required(VERSION 3.18)
project(fastchart LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(FASTCHART_APP "Build the interactive desktop app (needs GLFW + OpenGL)" ON)
option(FASTCHART_PYTHON "Build the Python module (needs pybind11 + OSMesa)" OFF)
option(FASTCHART_TESTS "Build the C++ tests" ON)

# Core: data store, pyramid, M4, candles (no OpenGL needed)
add_library(fastchart_core STATIC src/series.cpp)
target_include_directories(fastchart_core PUBLIC include)
set_target_properties(fastchart_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(EMSCRIPTEN)
  # Browser demo: C++ -> WebAssembly, OpenGL ES 3 -> WebGL 2
  add_executable(fastchart_web src/app.cpp src/renderer.cpp)
  target_link_libraries(fastchart_web fastchart_core)
  set_target_properties(fastchart_web PROPERTIES SUFFIX ".js" OUTPUT_NAME "fastchart")
  target_link_options(fastchart_web PRIVATE
    -sUSE_GLFW=3 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=134217728 -sMODULARIZE=1 -sEXPORT_NAME=createFastchart
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']" "-sEXPORTED_FUNCTIONS=['_malloc','_free','_main']")
  return()
endif()

if(FASTCHART_TESTS)
  enable_testing()
  add_executable(fastchart_tests tests/test_series.cpp)
  target_link_libraries(fastchart_tests fastchart_core)
  add_test(NAME core COMMAND fastchart_tests)
endif()

if(FASTCHART_APP)
  find_package(OpenGL REQUIRED)
  find_package(glfw3 3.3 REQUIRED)
  add_executable(fastchart_app src/app.cpp src/renderer.cpp)
  target_link_libraries(fastchart_app fastchart_core glfw OpenGL::GL)
endif()

if(FASTCHART_PYTHON)
  find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
  find_package(pybind11 CONFIG REQUIRED)
  find_library(OSMESA_LIB OSMesa REQUIRED)
  pybind11_add_module(_fastchart src/python.cpp src/renderer.cpp)
  target_link_libraries(_fastchart PRIVATE fastchart_core ${OSMESA_LIB})
  install(TARGETS _fastchart DESTINATION fastchart)
endif()
