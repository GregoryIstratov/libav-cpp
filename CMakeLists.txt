cmake_minimum_required(VERSION 3.20)
project(libav_cpp)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions")

if (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
endif ()


file(GLOB AV_FILES av/*.hpp)

if(LIBAV_CPP_ENABLE_EXAMPLES)
    add_subdirectory(examples)
endif()


add_library(av-cpp INTERFACE ${AV_FILES})
target_include_directories(av-cpp INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)