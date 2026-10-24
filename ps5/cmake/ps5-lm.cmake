# Luigi's Mansion for the PlayStation 5 - what the console adds to lm-recomp's own build.
#
# ps5-lm/tools/build-lm.sh configures lm-recomp itself with the payload SDK's toolchain and
# names this file as CMAKE_PROJECT_lmport_INCLUDE: CMake includes it right after lm-recomp's
# project() call, before Aurora looks for its dependencies. It provides the three Aurora cannot
# fetch for the console (after PS5_Dusklight's ps5/cmake/ps5-game.cmake):
#
#   Dawn    PS5_Dusklight/.deps/dawn, patched (patches/dawn.patch): Vulkan only, on RADV
#   SDL3    .deps/SDL, patched (patches/sdl.patch), with the drivers in ps5/sdl
#   nod     ps5/nod: a plain GameCube .iso reader with nod's C API
#
# and makes the executable's link PS5_Vulkan's (ps5/tools/link-title.sh, with RADV). The game is
# lm-recomp's static developer build, lmrecomp, from sources lmrecompile wrote on the host
# (LM_RECOMP_PREGEN).
#
# SPDX-License-Identifier: MIT

foreach(required PS5_PORT_DIR PS5_VULKAN_DIR DAWN_DIR SDL_DIR LM_RECOMP_PREGEN)
	if(NOT ${required})
		message(FATAL_ERROR "ps5-lm.cmake: ${required} is not set (use ps5-lm/tools/build-lm.sh)")
	endif()
endforeach()
set(ps5 ${PS5_PORT_DIR}/ps5)

# -fno-omit-frame-pointer: the console's crash backtraces are frame-pointer walks.
add_compile_options(-g1 -ffunction-sections -fdata-sections -fno-omit-frame-pointer)
# An init array, in the order the source has: ps5/tools/cc-init-array.sh has the reason
set(CMAKE_C_COMPILER_LAUNCHER sh ${ps5}/tools/cc-init-array.sh)
set(CMAKE_CXX_COMPILER_LAUNCHER sh ${ps5}/tools/cc-init-array.sh)
# The SDK's libc++ is LLVM 18's, where std::jthread and std::stop_token (Aurora's threads) are
# still experimental: link-title.sh links libc++experimental.a
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fexperimental-library>)

# The console: klog, the log file, the pad, the title's exit, and the libc names a title cannot import
add_library(ps5_platform STATIC ${ps5}/src/platform.c ${ps5}/src/shims.c ${ps5}/src/lm_shims.c
	${ps5}/src/startup.c ${ps5}/src/log.c)
target_include_directories(ps5_platform PUBLIC ${ps5}/src)
target_include_directories(ps5_platform PRIVATE ${SDL_DIR}/include)
file(READ ${ps5}/sce_sys/param.json param_json)
string(JSON PS5_TITLE_NAME GET "${param_json}" localizedParameters en-US titleName)
target_compile_definitions(ps5_platform PRIVATE "PS5_TITLE_NAME=\"${PS5_TITLE_NAME}\"")
set_target_properties(ps5_platform PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
# By its file, not its target: it is linked whole into the title (below)
set(ps5_platform_archive ${CMAKE_BINARY_DIR}/libps5_platform.a)

# Dawn: the Vulkan backend alone, as one static library. No window system: the surface is
# VK_KHR_display.
set(DAWN_PLATFORM_PS5 ON CACHE INTERNAL "")
set(DAWN_FETCH_DEPENDENCIES OFF CACHE INTERNAL "")
set(DAWN_BUILD_MONOLITHIC_LIBRARY STATIC CACHE INTERNAL "")
set(DAWN_ENABLE_VULKAN ON CACHE INTERNAL "")
foreach(off DAWN_ENABLE_D3D11 DAWN_ENABLE_D3D12 DAWN_ENABLE_METAL DAWN_ENABLE_NULL
		DAWN_ENABLE_DESKTOP_GL DAWN_ENABLE_OPENGLES DAWN_ENABLE_WEBGPU_ON_WEBGPU
		DAWN_USE_X11 DAWN_USE_WAYLAND DAWN_USE_GLFW DAWN_BUILD_SAMPLES DAWN_BUILD_TESTS
		DAWN_BUILD_BENCHMARKS DAWN_BUILD_PROTOBUF DAWN_ENABLE_INSTALL DAWN_WERROR
		DAWN_ENABLE_SPIRV_VALIDATION
		TINT_BUILD_CMD_TOOLS TINT_BUILD_TESTS TINT_BUILD_BENCHMARKS TINT_BUILD_IR_BINARY
		TINT_BUILD_GLSL_WRITER TINT_BUILD_GLSL_VALIDATOR TINT_BUILD_HLSL_WRITER
		TINT_BUILD_MSL_WRITER TINT_BUILD_NULL_WRITER TINT_BUILD_SPV_READER TINT_BUILD_TINTD)
	set(${off} OFF CACHE INTERNAL "")
endforeach()
add_subdirectory(${DAWN_DIR} ${CMAKE_BINARY_DIR}/dawn EXCLUDE_FROM_ALL)
set(AURORA_DAWN_PROVIDER vendor CACHE STRING "" FORCE)
set(AURORA_DAWN_LINKAGE static CACHE STRING "" FORCE)

# SDL3: threads, time and files as a FreeBSD has them; video, audio and the pad from ps5/sdl
set(SDL_PS5_PORT_DIR ${ps5}/sdl CACHE INTERNAL "")
set(SDL_PS5_PLATFORM_DIR ${ps5}/src CACHE INTERNAL "")
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)
foreach(off SDL_SHARED SDL_TEST_LIBRARY SDL_TESTS SDL_EXAMPLES SDL_INSTALL
		SDL_X11 SDL_WAYLAND SDL_KMSDRM SDL_RPI SDL_ROCKCHIP SDL_VIVANTE SDL_OFFSCREEN SDL_DUMMYVIDEO
		SDL_OPENGL SDL_OPENGLES SDL_VULKAN SDL_GPU SDL_RENDER_GPU SDL_CAMERA SDL_OPENVR
		SDL_OSS SDL_ALSA SDL_JACK SDL_PIPEWIRE SDL_PULSEAUDIO SDL_SNDIO SDL_DISKAUDIO SDL_DUMMYAUDIO
		SDL_DBUS SDL_IBUS SDL_LIBURING SDL_LIBUDEV SDL_HIDAPI SDL_HIDAPI_LIBUSB SDL_VIRTUAL_JOYSTICK
		SDL_DLOPEN_NOTES SDL_DEPS_SHARED SDL_ASSEMBLY_UNUSED)
	set(${off} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory(${SDL_DIR} ${CMAKE_BINARY_DIR}/sdl EXCLUDE_FROM_ALL)
set(AURORA_SDL3_PROVIDER vendor CACHE STRING "" FORCE)
set(AURORA_SDL3_LINKAGE static CACHE STRING "" FORCE)

# nod
add_library(nod_ps5 STATIC ${ps5}/nod/nod.cpp)
target_include_directories(nod_ps5 PUBLIC ${ps5}/nod/include)
target_compile_features(nod_ps5 PRIVATE cxx_std_20)
add_library(nod::nod ALIAS nod_ps5)
set(AURORA_NOD_PROVIDER vendor CACHE STRING "" FORCE)
set(AURORA_NOD_LINKAGE static CACHE STRING "" FORCE)

# Tracy (compiled out, but still configured) asks a FreeBSD for libexecinfo: the console has
# none, and nothing of it is called
set(EXECINFO_LIBRARY ${ps5_platform_archive} CACHE STRING "" FORCE)

# zstd's tracing hooks are weak symbols for a tool to fill in: a title cannot leave a symbol
# undefined (once Aurora has fetched zstd, at the end of the configuration)
function(ps5_late_fixes)
	if(TARGET libzstd_static)
		target_compile_definitions(libzstd_static PRIVATE ZSTD_TRACE=0)
	endif()
	# The link reads the platform archive by its file: it has to be there first
	if(TARGET lmrecomp)
		add_dependencies(lmrecomp ps5_platform)
	endif()
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL ps5_late_fixes)

# The link: every object and archive CMake would hand the linker goes to PS5_Vulkan's recipe
# instead (RADV, the platform layer, the CRT, the fake SELF)
set(CMAKE_CXX_LINK_EXECUTABLE
	"env LM_SOURCE_DIR=${CMAKE_SOURCE_DIR} bash ${ps5}/tools/link-title.sh ${CMAKE_BINARY_DIR} ${PS5_VULKAN_DIR} ${PS5_PAYLOAD_SDK} <OBJECTS> --whole-archive ${ps5_platform_archive} --no-whole-archive <LINK_LIBRARIES>")
