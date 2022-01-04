cmake_minimum_required(VERSION 3.21.0)

set(CMAKE_C_COMPILER "clang")
set(SOURCE_DIR ${PROJECT_SOURCE_DIR}/src)

## fix later for debug build etc
set(CMAKE_C_FLAGS "-std=c11")

find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})

if ( ${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.11.0" ) 
  file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS ${SOURCE_DIR}/*.c ${SOURCE_DIR}/*.h)
else() 
  file(GLOB SOURCE_FILES ${SOURCE_DIR}/*.c ${SOURCE_DIR}/*.h ${SOURCE_DIR}/*/*.h */*.cpp)
endif()

include_directories(${SOURCE_DIR})

add_library(noroshi SHARED
  ${SOURCE_FILES}
)
