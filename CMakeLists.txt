cmake_minimum_required(VERSION 3.0)

# Nom du projet
project(SugarConvDsk)

set (gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set (CMAKE_INSTALL_PREFIX ${SugarConvDsk_BINARY_DIR}/Install)
set (SKIP_INSTALL_ALL ON)

add_subdirectory(zlib)
add_subdirectory(SugarConvDsk)

# Graphical front end : always on Windows, SDL2 development build elsewhere on request
option(SUGARCONV_GUI_SDL "Build the SDL2 development GUI (non-Windows)" OFF)
if(WIN32 OR SUGARCONV_GUI_SDL)
	add_subdirectory(SugarConvGui)
endif()
add_subdirectory(googletest/googletest)
add_subdirectory(Tests)


