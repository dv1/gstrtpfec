cmake_minimum_required(VERSION 2.8.1)

project(gstrtpfec C)


set(COMMON_CFLAGS "-Wextra -Wall -std=c99 -pedantic")


if (DEBUG STREQUAL "ON")

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_C_FLAGS "${COMMON_CFLAGS} -O0 -g3 -ggdb")
message(STATUS "Debug mode ON" )

else(DEBUG STREQUAL "ON")

set(CMAKE_BUILD_TYPE Release)
set(CMAKE_C_FLAGS "${COMMON_CFLAGS} -O2")
message(STATUS "Debug mode OFF")

endif (DEBUG STREQUAL "ON")


find_package(PkgConfig)
pkg_check_modules(GLIB2 glib-2.0)
pkg_check_modules(GTHREAD2 gthread-2.0)
pkg_check_modules(GSTREAMER gstreamer-0.10)
pkg_check_modules(GSTREAMERRTP gstreamer-rtp-0.10)

set(GLIB2_INC ${GLIB2_INCLUDE_DIRS} ${GTHREAD2_INCLUDE_DIRS})
set(GLIB2_LIB ${GLIB2_LIBRARIES} ${GTHREAD2_LIBRARIES})

set(GSTREAMER_INC ${GSTREAMER_INCLUDE_DIRS} ${GSTREAMERRTP_INCLUDE_DIRS})
set(GSTREAMER_LIBDIR ${GSTREAMER_LIBRARY_DIRS} ${GSTREAMERRTP_LIBRARY_DIRS})
set(GSTREAMER_LIB ${GSTREAMER_LIBRARIES} ${GSTREAMERRTP_LIBRARIES})


set(OPENFEC_INCLUDE_PATH ${CMAKE_INSTALL_PREFIX}/include CACHE PATH "Path where openfec/lib_common/of_openfec_api.h can be accessed")
set(OPENFEC_LIBRARY_PATH ${CMAKE_INSTALL_PREFIX}/lib CACHE PATH "Path where the openfec library can be accessed")
set(PLUGIN_INSTALL_PATH ${CMAKE_INSTALL_PREFIX}/lib/gstreamer-0.10 CACHE PATH "Where to install the GStreamer plugin")

file (GLOB_RECURSE sources *)
include_directories(${OPENFEC_INCLUDE_PATH} ${GSTREAMER_INC} ${GLIB2_INC})
link_directories(${OPENFEC_LIBRARY_PATH} ${GSTREAMER_LIBDIR} ${GLIB2_LIBDIR})
add_library(gstrtpfec SHARED ${sources})
target_link_libraries(gstrtpfec openfec ${GLIB2_LIB} ${GSTREAMER_LIB})

install(TARGETS gstrtpfec DESTINATION ${PLUGIN_INSTALL_PATH})

