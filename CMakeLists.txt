cmake_minimum_required (VERSION 3.9)
project (wvnc)

set (CMAKE_EXPORT_COMPILE_COMMANDS ON)
set (CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
find_package (Wayland REQUIRED)
find_package (WaylandScanner REQUIRED)
find_package (PkgConfig REQUIRED)
pkg_search_module (LIBVNCSERVER REQUIRED libvncserver)
pkg_search_module (XKBCOMMON REQUIRED xkbcommon)

option (WITH_ASAN "Enable ASan" OFF)

if (WITH_ASAN)
	set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-omit-frame-pointer -fsanitize=address")
endif ()

ecm_add_wayland_client_protocol (
	WLR_SCREENCOPY_SRC
	PROTOCOL wlr-protocols/unstable/wlr-screencopy-unstable-v1.xml
	BASENAME wlr-screencopy
)

ecm_add_wayland_client_protocol (
	XDG_OUTPUT_SRC
	PROTOCOL wayland-protocols/unstable/xdg-output/xdg-output-unstable-v1.xml
	BASENAME xdg-output
)

ecm_add_wayland_client_protocol (
	VIRTUAL_KEYBOARD_SRC
	PROTOCOL virtual-keyboard-unstable-v1.xml
	BASENAME virtual-keyboard
)

set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ggdb")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter -Wno-unknown-pragmas -Wno-unused")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")

include_directories ("${CMAKE_BINARY_DIR}")
include_directories (${LIBVNCSERVER_INCLUDEDIR})
include_directories (${XKBCOMMON_INCLUDEDIR})

add_executable (wvnc main.c buffer.c utils.c uinput.c ${VIRTUAL_KEYBOARD_SRC}
	${WLR_SCREENCOPY_SRC} ${XDG_OUTPUT_SRC})
target_link_libraries (wvnc rt m ${Wayland_LIBRARIES} ${LIBVNCSERVER_LIBRARIES}
	${XKBCOMMON_LIBRARIES})

install (TARGETS wvnc RUNTIME DESTINATION bin COMPONENT bin)
