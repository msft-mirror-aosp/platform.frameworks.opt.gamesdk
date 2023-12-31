cmake_minimum_required(VERSION 3.4.1)
project(tuningfork-demo)

find_package(games-performance-tuner REQUIRED CONFIG)
find_package(games-frame-pacing REQUIRED CONFIG)

# If you have protobuf installed from a different directory, set it here. The source version
#  must match the protoc version.
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -Werror -Wthread-safety" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D _LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS -Os -fPIC" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DGOOGLE_PROTOBUF_NO_RTTI -DHAVE_PTHREAD")

set( GAMESDK_BUILD_TUNINGFORK ON)

# Uncomment to add the Game SDK sources as part of the project sources, allowing to develop
# (with auto completions) and debug Tuning Fork from Android Studio using this sample.
#add_gamesdk_sources()

include("../../../../games-performance-tuner/protobuf/protobuf.cmake")

protobuf_generate_lite_cpp( ${CMAKE_CURRENT_SOURCE_DIR}/src/main/proto src/main/proto/dev_tuningfork.proto)
protobuf_generate_lite_cpp( ${CMAKE_CURRENT_SOURCE_DIR}/src/main/proto src/main/proto/tuningfork.proto)
include_directories(${PROTO_GENS_DIR})
include_directories(${PROTOBUF_SRC_DIR})
include_directories(${PROTOBUF_SRC_DIR}/../config/)
include_directories(../../common)
include_directories(../../../common/include)
include_directories(../../../../include)

# Note that we use the FULL version of protobuf in the testapp, so we can get debug strings
add_library( protobuf-static
             STATIC
             ${PROTOBUF_LITE_SRCS}
)
target_compile_options(protobuf-static PUBLIC "-Wno-tautological-constant-compare" "-Wno-enum-compare-switch" "-Wno-invalid-noreturn")

add_library( ${CMAKE_PROJECT_NAME}
             SHARED
             src/main/cpp/experimentsdemo.cpp
             ../../common/Renderer.cpp
             ../../common/Scene.cpp
             ${PROTO_GENS_DIR}/lite/dev_tuningfork.pb.cc
             ${PROTO_GENS_DIR}/lite/tuningfork.pb.cc
)

target_link_libraries( ${CMAKE_PROJECT_NAME}
        games-frame-pacing::swappy_static
        games-performance-tuner::tuningfork_static
        protobuf-static
        android
        EGL
        GLESv2
        log
)
