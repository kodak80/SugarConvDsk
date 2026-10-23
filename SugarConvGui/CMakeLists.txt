cmake_minimum_required(VERSION 3.5)
project(SugarConvGui CXX C)

# Windows : SugarConvGui.exe (Win32 + Direct3D 11 + Dear ImGui), a single static executable.
# Linux   : SugarConvGuiSDL, a development front end on SDL2 (configure with -DSUGARCONV_GUI_SDL=ON).

set(CORE_DIR ${CMAKE_CURRENT_LIST_DIR}/../CPCCoreEmu)
set(CORE_SRCS
	${CORE_DIR}/CAPSFile.cpp
	${CORE_DIR}/CRC.cpp
	${CORE_DIR}/DiskBuilder.cpp
	${CORE_DIR}/DiskContainer.cpp
	${CORE_DIR}/DiskGen.cpp
	${CORE_DIR}/FileAccess.cpp
	${CORE_DIR}/FormatTypeCTRAW.cpp
	${CORE_DIR}/FormatTypeEDSK.cpp
	${CORE_DIR}/FormatTypeDSK.cpp
	${CORE_DIR}/FormatTypeHFE.cpp
	${CORE_DIR}/FormatTypeIPF.cpp
	${CORE_DIR}/FormatTypeRAW.cpp
	${CORE_DIR}/FormatTypeSCP.cpp
	${CORE_DIR}/IDisk.cpp
	${CORE_DIR}/MediaManager.cpp
)

set(IMGUI_DIR ${CMAKE_CURRENT_LIST_DIR}/third_party/imgui)
set(IMGUI_SRCS
	${IMGUI_DIR}/imgui.cpp
	${IMGUI_DIR}/imgui_draw.cpp
	${IMGUI_DIR}/imgui_tables.cpp
	${IMGUI_DIR}/imgui_widgets.cpp
)

set(APP_SRCS
	src/App.cpp
	src/App.h
	src/Converter.cpp
	src/Converter.h
)

# Version shown in Help > About
if(NOT SCG_VERSION)
	find_package(Git QUIET)
	if(GIT_FOUND)
		execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
		                OUTPUT_VARIABLE SCG_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
	endif()
endif()
if(NOT SCG_VERSION)
	set(SCG_VERSION "dev")
endif()

if(WIN32)
	add_executable(SugarConvGui WIN32
		src/main_win32.cpp
		src/app.rc
		src/app.manifest
		${APP_SRCS}
		${CORE_SRCS}
		${IMGUI_SRCS}
		${IMGUI_DIR}/backends/imgui_impl_win32.cpp
		${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
	)
	set(GUI_TARGET SugarConvGui)
	target_link_libraries(SugarConvGui d3d11 dxgi d3dcompiler shell32 ole32 dwmapi zlibstatic)
else()
	find_package(SDL2 REQUIRED CONFIG)
	add_executable(SugarConvGuiSDL
		src/main_sdl.cpp
		${APP_SRCS}
		${CORE_SRCS}
		${IMGUI_SRCS}
		${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
		${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp
	)
	set(GUI_TARGET SugarConvGuiSDL)
	target_link_libraries(SugarConvGuiSDL SDL2::SDL2 m stdc++fs libz.a pthread)
endif()

target_compile_definitions(${GUI_TARGET} PRIVATE SCG_VERSION="${SCG_VERSION}")
target_include_directories(${GUI_TARGET} PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/src
	${CORE_DIR}
	${IMGUI_DIR}
	${IMGUI_DIR}/backends
	${CMAKE_CURRENT_LIST_DIR}/../zlib
	${CMAKE_CURRENT_BINARY_DIR}/../zlib
)
set_property(TARGET ${GUI_TARGET} PROPERTY CXX_STANDARD 17)
set_property(TARGET ${GUI_TARGET} PROPERTY CXX_STANDARD_REQUIRED ON)

if(MSVC)
	target_compile_options(${GUI_TARGET} PRIVATE /utf-8 /MP)
	set_property(TARGET ${GUI_TARGET} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
