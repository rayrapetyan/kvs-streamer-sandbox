cmake_minimum_required(VERSION 3.18)
project(kvs_streamer)

set(CMAKE_VERBOSE_MAKEFILE "ON")

set(CMAKE_CXX_STANDARD 20)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

find_package(PkgConfig REQUIRED)

pkg_check_modules(GST gstreamer-1.0)
if(GST_FOUND)
    if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:/usr/local/opt/libffi/lib/pkgconfig")
        find_library(Intl "intl" REQUIRED PATHS "/usr/local/opt/gettext/lib")
        link_directories("/usr/local/opt/gettext/lib")
    endif()

    pkg_check_modules(GLIB2 REQUIRED glib-2.0)
    pkg_check_modules(GST_APP REQUIRED gstreamer-app-1.0)
    pkg_check_modules(GOBJ2 REQUIRED gobject-2.0)

    set(OPEN_SRC_INCLUDE_DIRS
            ${OPEN_SRC_INCLUDE_DIRS} ${GLIB2_INCLUDE_DIRS} ${GST_INCLUDE_DIRS}
            ${GST_APP_INCLUDE_DIRS} ${GOBJ2_INCLUDE_DIRS})

    set(GST_SAMPLE_LIBRARIES ${GLIB2_LIBRARIES} ${GST_LIBRARIES}
            ${GST_APP_LIBRARIES} ${GOBJ2_LIBRARIES} ${Intl})

    link_directories(${GLIB2_LIBRARY_DIRS})
    link_directories(${GST_LIBRARY_DIRS})
    link_directories(${GST_APP_LIBRARY_DIRS})
    link_directories(${GOBJ2_LIBRARY_DIRS})
else()
    message( FATAL_ERROR "gstreamer not found" )
endif()

include_directories(/src/amazon-kinesis-video-streams-webrtc-sdk-c/src/include)
include_directories(/src/amazon-kinesis-video-streams-webrtc-sdk-c/open-source/include)
include_directories(/usr/local/include)
include_directories(/usr/include/libevdev-1.0)
include_directories(${OPEN_SRC_INCLUDE_DIRS})

link_directories(/src/amazon-kinesis-video-streams-webrtc-sdk-c/src/lib)
link_directories(/src/amazon-kinesis-video-streams-webrtc-sdk-c/open-source/lib)
link_directories(/src/amazon-kinesis-video-streams-webrtc-sdk-c/cmake-build-release)

add_executable(
        kvs-streamer
        /src/amazon-kinesis-video-streams-webrtc-sdk-c/samples/Common.c
        utils.cpp
        input.cpp
        main.cpp
)

target_link_libraries(kvs-streamer kvsWebrtcClient kvsWebrtcSignalingClient ${GST_SAMPLE_LIBRARIES} kvsCommonLws kvspicUtils websockets clipboard X11 Xtst Xrandr xcb)
