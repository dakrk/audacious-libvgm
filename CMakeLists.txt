cmake_minimum_required(VERSION 3.22)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

project(audacious-libvgm
	VERSION 0.1.0
	LANGUAGES C CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	add_compile_options(-ffunction-sections -Wall)
	add_link_options(-Wl,--gc-sections -Wl,-O1)
endif()

add_library(audacious-libvgm MODULE
	VGMPlugin.cpp
	VGMPlugin.hpp
	VFSLoader.cpp
	VFSLoader.hpp
)

find_package(PkgConfig REQUIRED)
pkg_search_module(AUDACIOUS REQUIRED audacious>=4.2)
pkg_get_variable(AUDACIOUS_PLUGIN_DIR audacious plugin_dir)

find_package(ZLIB REQUIRED)

set(BUILD_LIBAUDIO OFF)
set(BUILD_TESTS OFF)
set(BUILD_PLAYER OFF)
set(BUILD_VGM2WAV OFF)
set(LIBRARY_TYPE "STATIC")
add_subdirectory(libvgm)

set_target_properties(audacious-libvgm PROPERTIES PREFIX "" OUTPUT_NAME "libvgm")
target_compile_features(audacious-libvgm PUBLIC cxx_std_23)
target_include_directories(audacious-libvgm PRIVATE ${AUDACIOUS_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS})
target_link_libraries(audacious-libvgm PUBLIC ${AUDACIOUS_LINK_LIBRARIES} ${ZLIB_LIBRARIES} vgm-utils vgm-emu vgm-player)

# cannot exclude libvgm from installing its headers and other crap THANK YOU CMAKE
install(TARGETS audacious-libvgm DESTINATION ${AUDACIOUS_PLUGIN_DIR}/Input)
