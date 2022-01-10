cmake_minimum_required(VERSION 3.21.0)

set(CMAKE_C_COMPILER "clang")
set(SOURCE_DIR ${PROJECT_SOURCE_DIR}/src)

## fix later for debug build etc
set(CMAKE_C_FLAGS "-std=c11")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(noroshi)

zephyr_include_directories(src)

include_directories(${SOURCE_DIR})

zephyr_library_include_directories(
  src/lwm2m
  src/ui
  src/events
  src/hw
  src/module
)

add_subdirectory(src/ui)
add_subdirectory(src/event)
add_subdirectory(src/module)
add_subdirectory(src/hw)
#add_subdirectory(src/lwm2m) nah

add_subdirectory(external)

target_sources(noroshi PRIVATE src/main.c)
