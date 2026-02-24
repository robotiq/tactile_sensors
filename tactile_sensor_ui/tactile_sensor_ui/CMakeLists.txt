cmake_minimum_required(VERSION 3.16)
project(tactile_sensor_ui LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets SerialPort)
find_package(OpenMP)

if(WIN32)
    # On Windows, download and build dependencies from source via FetchContent.
    include(FetchContent)

    # FFTW — official release tarball (git repo requires OCaml codegen tooling)
    FetchContent_Declare(fftw3
        URL https://www.fftw.org/fftw-3.3.10.tar.gz
        URL_HASH MD5=8ccbf6a5ea78a16dbc3e1306e234cc5c
    )
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(fftw3)

    # MathGL 2.5 — latest 2.x release, API-compatible with older 2.3.x
    FetchContent_Declare(mathgl
        URL https://sourceforge.net/projects/mathgl/files/mathgl/mathgl%202.5/mathgl-2.5.tar.gz/download
        URL_HASH MD5=0cb370985768566c045939c11bb72fc4
        DOWNLOAD_NAME mathgl-2.5.tar.gz
    )
    set(enable-qt5 ON CACHE BOOL "" FORCE)
    set(enable-opengl OFF CACHE BOOL "" FORCE)
    set(enable-glut OFF CACHE BOOL "" FORCE)
    set(enable-fltk OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(mathgl)

    set(MATHGL_LIBRARIES mgl)
    set(FFTW3_LIBRARIES fftw3)
else()
    # On Linux, use system-installed packages (e.g. apt: libmgl-dev, libfftw3-dev).
    find_library(MATHGL_LIBRARY NAMES mgl REQUIRED)
    find_library(FFTW3_LIBRARY NAMES fftw3 REQUIRED)

    set(MATHGL_LIBRARIES ${MATHGL_LIBRARY})
    set(FFTW3_LIBRARIES ${FFTW3_LIBRARY})
endif()

set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/connection.cpp
    src/communicator.cpp
    src/graphics.cpp
    src/log.cpp
    images/images.qrc
)

set(HEADERS
    src/mainwindow.h
    src/communicator.h
    src/circular_buffer.h
    src/finger_data.h
)

add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
    src/mainwindow.ui
)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall)

if(OpenMP_CXX_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenMP::OpenMP_CXX)
endif()

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        Qt5::Widgets
        Qt5::SerialPort
        ${MATHGL_LIBRARIES}
        ${FFTW3_LIBRARIES}
)
