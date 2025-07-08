# CMAKE project for openrct2-ui (UI build of OpenRCT2)
cmake_minimum_required(VERSION 3.1)
if (CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(FATAL_ERROR "Building in-source is not supported! Create a build dir and remove ${CMAKE_SOURCE_DIR}/CMakeCache.txt")
endif ()

# Options
option(DISABLE_OPENGL "Disable OpenGL support.")

# Third party libraries
PKG_CHECK_MODULES(SDL2 REQUIRED sdl2)
PKG_CHECK_MODULES(SPEEX REQUIRED speexdsp)
if (NOT DISABLE_OPENGL)
    # GL doesn't work nicely with macOS, while find_package doesn't work with multiarch on Ubuntu.
    if (APPLE)
        find_package(OpenGL REQUIRED)
    elseif (NOT WIN32 AND NOT VITA)
        PKG_CHECK_MODULES(GL REQUIRED gl)
    endif ()
endif ()

# Sources
file(GLOB_RECURSE OPENRCT2_UI_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp")

if (APPLE)
    file(GLOB_RECURSE OPENRCT2_UI_MM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.mm")
    set_source_files_properties(${OPENRCT2_UI_MM_SOURCES} PROPERTIES COMPILE_FLAGS "-x objective-c++ -fmodules")
endif ()

# Outputs
set (PROJECT openrct2)
project(${PROJECT} CXX)
add_executable(${PROJECT} ${OPENRCT2_UI_SOURCES} ${OPENRCT2_UI_MM_SOURCES})

target_link_libraries(${PROJECT} "libopenrct2"
                                 ${SDL2_LDFLAGS}
                                 ${SPEEX_LDFLAGS})

if (APPLE)
    target_link_libraries(${PROJECT} "-framework Cocoa")
endif ()

if (NOT DISABLE_OPENGL)
    if (WIN32)
        target_link_libraries(${PROJECT} opengl32)
    elseif (APPLE)
        target_link_libraries(${PROJECT} ${OPENGL_LIBRARY})
    elseif (VITA)
        target_link_libraries(${PROJECT} libGLESv2_stub libIMGEGL_stub libgpu_es4_ext_stub)
    else ()
        target_link_libraries(${PROJECT} ${GL_LIBRARIES})
    endif ()
endif ()

# Includes
target_include_directories(${PROJECT} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/.."
                                              ${SPEEX_INCLUDE_DIRS})
target_include_directories(${PROJECT} SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})

# Compiler flags
if (WIN32)
    # mingw complains about "%zu" not being a valid format specifier for printf, unless we
    # tell it that it is
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__USE_MINGW_ANSI_STDIO=1")
endif ()

# Defines
if (DISABLE_OPENGL)
    add_definitions(-DDISABLE_OPENGL)
else ()
    # Makes OpenGL function get queried in run-time rather than linked-in
    add_definitions(-DOPENGL_NO_LINK)
endif ()

if(VITA)
target_link_libraries(${PROJECT} SDL2_image jpeg webp)
set(VITA_APP_NAME "OpenRCT2")
set(VITA_TITLEID  "OPRCT0002")
set(VITA_VERSION  "01.00")
vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE cursor.png cursor.png
)
endif()

if (WIIU)
    wut_create_rpx(${PROJECT})
endif()
