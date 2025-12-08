cmake_minimum_required(VERSION 3.10)
project(milkshmimview C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)
pkg_check_modules(ISIO REQUIRED ImageStreamIO)

include_directories(${GTK4_INCLUDE_DIRS} ${ISIO_INCLUDE_DIRS})
link_directories(${GTK4_LIBRARY_DIRS} ${ISIO_LIBRARY_DIRS})

add_executable(milkshmimview src/main.c)
target_link_libraries(milkshmimview ${GTK4_LIBRARIES} ${ISIO_LIBRARIES} m)
target_compile_options(milkshmimview PUBLIC ${GTK4_CFLAGS_OTHER})
