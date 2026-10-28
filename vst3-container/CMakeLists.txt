# Crate — VST3 plugin browser and container.
#
# Two build modes:
#
#  1. Core + tests only (no SDK needed). This is what CI and `scripts/build-tests.sh`
#     use, and it must keep working on a machine with nothing but a C++17 compiler:
#       cmake -S . -B build -DCRATE_BUILD_PLUGIN=OFF
#
#  2. The actual .vst3 bundle (needs the Steinberg VST3 SDK):
#       cmake -S . -B build -DCRATE_VST3_SDK_DIR=/path/to/vst3sdk
#     or let CMake fetch it (network + git submodules required):
#       cmake -S . -B build -DCRATE_FETCH_VST3_SDK=ON
#
# The bundle target is named Crate and, after building, appears as Crate.vst3 in the
# SDK's plugin output folder (see docs/VST3.md for where to copy it).
cmake_minimum_required(VERSION 3.20)
project(Crate VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(CRATE_BUILD_PLUGIN "Build the VST3 plugin (requires the VST3 SDK)" ON)
option(CRATE_BUILD_TESTS "Build the SDK-free unit tests" ON)
option(CRATE_FETCH_VST3_SDK "Fetch the VST3 SDK with FetchContent" OFF)
set(CRATE_VST3_SDK_DIR "" CACHE PATH "Path to the VST3 SDK root (the folder holding public.sdk)")
# Steinberg tags look like v<major>.<minor>.<patch>_build_<n>; the fetch below needs a real
# one (v3.7.11 has build_10, not build_40). Override with -DCRATE_VST3_SDK_TAG=...
set(CRATE_VST3_SDK_TAG "v3.7.11_build_10" CACHE STRING
	"Steinberg VST3 SDK git tag fetched when CRATE_FETCH_VST3_SDK is ON")

# Crate is raw-SDK: it never touches VSTGUI. The SDK defaults both of these to ON, which
# needs the vstgui4 submodule (the fetch below deliberately does not bring it in) and
# builds the SDK's world of example plug-ins. Flip them once here, so that every way of
# pointing this build at an SDK — a local checkout via CRATE_VST3_SDK_DIR or the fetch —
# configures the same thing. An explicit -D on the command line still wins.
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "VSTGUI is not used by Crate")
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "Skip the SDK example plug-ins")

# A .vst3 that someone installs must not depend on a Visual C++ Redistributable of the
# exact toolchain version. /MT only affects MSVC; other platforms ignore the option.
set(SMTG_USE_STATIC_CRT ON CACHE BOOL "Link the C runtime statically (/MT)")
# The SDK flips its own targets to /MT with add_compile_options inside its directory,
# so that never reaches targets created here. Both sides must agree or the link fails
# with LNK2038 RuntimeLibrary mismatch.
if (MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

# --------------------------------------------------------------------------------------
# Core library: registry readers, state codec, journal, engine. No SDK, no GUI, no audio
# device. If this does not compile, nothing else matters.
# --------------------------------------------------------------------------------------
add_library(crate_core STATIC
	source/app/cratepaths.cpp
	source/app/journal.cpp
	source/app/blacklist.cpp
	source/host/audioops.cpp
	source/host/chain.cpp
	source/host/params.cpp
	source/host/slot.cpp
	source/json/json.cpp
	source/registry/binaryinfo.cpp
	source/registry/entry.cpp
	source/registry/flreader.cpp
	source/registry/livereader.cpp
	source/registry/moduleinfo.cpp
	source/registry/reader.cpp
	source/registry/reaperreader.cpp
	source/registry/registryservice.cpp
	source/state/containerstate.cpp
	source/util/fs.cpp
	source/util/search.cpp
	source/util/stringutil.cpp
)

target_include_directories(crate_core PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/source
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite
)

# SQLite is compiled in: the Ableton reader opens the host's cache read-only and a
# shared-library dependency inside a DAW's plugin process is a support ticket.
add_library(crate_sqlite STATIC third_party/sqlite/sqlite3.c)
target_include_directories(crate_sqlite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite)
target_compile_definitions(crate_sqlite PRIVATE
	SQLITE_THREADSAFE=1
	SQLITE_OMIT_LOAD_EXTENSION
	SQLITE_DEFAULT_MEMSTATUS=0
)
target_link_libraries(crate_core PUBLIC crate_sqlite)

if (APPLE)
	target_link_libraries(crate_core PUBLIC "-framework CoreFoundation" "-framework Foundation")
elseif (WIN32)
	target_link_libraries(crate_core PUBLIC shlwapi)
endif()

if (CRATE_BUILD_TESTS)
	enable_testing()
	add_executable(crate_tests
		tests/test_main.cpp
		tests/test_app.cpp
		tests/test_chain.cpp
		tests/test_fl.cpp
		tests/test_json.cpp
		tests/test_live.cpp
		tests/test_moduleinfo.cpp
		tests/test_params.cpp
		tests/test_reaper.cpp
		tests/test_service.cpp
		tests/test_slot.cpp
		tests/test_state.cpp
		tests/test_util.cpp
	)
	target_include_directories(crate_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
	target_link_libraries(crate_tests PRIVATE crate_core)
	# Absolute, forward-slashed fixture path — the override tests/test_util.h already expects.
	# Without it the header falls back to a path relative to __FILE__, which the Visual Studio
	# generator records relative to the project files, so ctest (starting the binary in the
	# build tree) would look for tests/fixtures in the wrong place.
	target_compile_definitions(crate_tests PRIVATE
		"CRATE_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures\"")
	if (MSVC)
		target_compile_options(crate_tests PRIVATE /W4)
	else()
		target_compile_options(crate_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
	endif()
	add_test(NAME crate_tests COMMAND crate_tests)
endif()


# --------------------------------------------------------------------------------------
# The plugin itself. Only built when the SDK is actually available: a missing SDK must
# never make the core unbuildable.
# --------------------------------------------------------------------------------------
if (CRATE_BUILD_PLUGIN)
	if (CRATE_FETCH_VST3_SDK AND NOT CRATE_VST3_SDK_DIR)
		include (FetchContent)
		FetchContent_Declare (vst3sdk
			GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
			GIT_TAG ${CRATE_VST3_SDK_TAG}
			GIT_SHALLOW TRUE
			GIT_SUBMODULES base pluginterfaces public.sdk cmake
			# Not recursive: the four submodules above pull nested repositories that Crate
			# never links, and a nested fetch of one that wants credentials hangs the build
			# instead of failing it.
			GIT_SUBMODULES_RECURSE FALSE
		)
		FetchContent_MakeAvailable (vst3sdk)
		set (CRATE_VST3_SDK_DIR ${vst3sdk_SOURCE_DIR})
	endif ()

	set (_crate_sdk_header "${CRATE_VST3_SDK_DIR}/public.sdk/source/vst/vstaudioeffect.h")
	if (CRATE_VST3_SDK_DIR AND EXISTS "${_crate_sdk_header}")
		if (NOT COMMAND smtg_add_vst3plugin)
			# Bundle layout (Contents/x86_64-win, Info.plist, codesign hooks) is platform
			# work the SDK's CMake already does correctly. Do not hand-roll it.
			add_subdirectory (${CRATE_VST3_SDK_DIR} ${CMAKE_BINARY_DIR}/vst3sdk)
		endif ()
		if (NOT COMMAND smtg_add_vst3plugin)
			message (FATAL_ERROR
				"The VST3 SDK at ${CRATE_VST3_SDK_DIR} did not provide smtg_add_vst3plugin()")
		endif ()

		# The hosting library leaves these two to the host application: the platform module
# implementation that Module::create comes from, and the memory stream the state code
# uses. The SDK validator compiles the same files for the same reason.
set (_crate_hosting_extra
    ${CRATE_VST3_SDK_DIR}/public.sdk/source/common/memorystream.cpp)
if (WIN32)
    list (APPEND _crate_hosting_extra ${CRATE_VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_win32.cpp)
elseif (APPLE)
    list (APPEND _crate_hosting_extra ${CRATE_VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_mac.mm)
else ()
    list (APPEND _crate_hosting_extra ${CRATE_VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_linux.cpp)
endif ()

smtg_add_vst3plugin (Crate
			source/vst3/crateentry.cpp
			source/vst3/cratecontroller.cpp
			source/vst3/crateprocessor.cpp
			source/vst3/hostcontext.cpp
			source/vst3/vst3instance.cpp
			source/vst3/vst3loader.cpp
    ${_crate_hosting_extra}
		)
		target_include_directories (Crate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source)
		# `sdk` is the SDK's static library target: it carries pluginfactory.cpp,
		# moduleinit.cpp (InitModule/DeinitModule) and stringconvert, and the platform main
		# (ModuleEntry / InitDll) is added to our target by smtg_add_vst3plugin.
		if (TARGET sdk)
			target_link_libraries (Crate PRIVATE sdk crate_core)
        # HostApplication and VST3::Hosting::Module live in the hosting library.
        if (TARGET sdk_hosting)
            target_link_libraries (Crate PRIVATE sdk_hosting)
        endif ()
		else ()
			target_link_libraries (Crate PRIVATE crate_core)
			message (WARNING "The VST3 SDK did not define its 'sdk' target; linking only crate_core")
		endif ()
		target_compile_definitions (Crate PRIVATE CRATE_VERSION="${PROJECT_VERSION}")
		set_target_properties (Crate PROPERTIES
			CXX_VISIBILITY_PRESET hidden
			VISIBILITY_INLINES_HIDDEN ON
		)
		# moduleinfo.json lets other plugin browsers show Crate with a name, vendor and
		# category instead of guessing from the binary.
		if (COMMAND smtg_target_generate_moduleinfo)
			smtg_target_generate_moduleinfo (Crate)
		endif ()

		# --- integration check -------------------------------------------------------
		# A fixture plugin plus a program that loads it through the container's own state
		# format. This is the only automated test that exercises Module::create, real
		# IComponent/IAudioProcessor calls and the latency-compensated dry path together.
		option (CRATE_BUILD_INTEGRATION_TESTS "Build the SDK-linked hosting integration check" ON)
		if (CRATE_BUILD_INTEGRATION_TESTS)
			# The hosting library leaves these two to the host application: the platform module
# implementation that Module::create comes from, and the memory stream the state code
# uses. The SDK validator compiles the same files for the same reason.
set (_crate_hosting_extra
    ${CRATE_VST3_SDK_DIR}/public.sdk/source/common/memorystream.cpp)
if (WIN32)
    list (APPEND _crate_hosting_extra ${CRATE_VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_win32.cpp)
elseif (APPLE)
    list (APPEND _crate_hosting_extra ${CRATE_VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_mac.mm)
else ()
    list (APPEND _crate_hosting_extra ${CRATE_VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_linux.cpp)
endif ()

smtg_add_vst3plugin (CrateGainFixture tests/integration/gainplugin.cpp ${_crate_plugin_main})
			target_include_directories (CrateGainFixture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration)
			if (TARGET sdk)
				target_link_libraries (CrateGainFixture PRIVATE sdk)
			endif ()

			add_executable (crate_hosting_check tests/integration/hosting_check.cpp)
target_sources (crate_hosting_check PRIVATE ${_crate_hosting_extra})
			target_include_directories (crate_hosting_check PRIVATE
				${CMAKE_CURRENT_SOURCE_DIR}/tests/integration
				${CMAKE_CURRENT_SOURCE_DIR}/source)
			target_link_libraries (crate_hosting_check PRIVATE crate_core)
            if (TARGET sdk_hosting)
                target_link_libraries (crate_hosting_check PRIVATE sdk_hosting)
            endif ()
			if (TARGET sdk)
				target_link_libraries (crate_hosting_check PRIVATE sdk)
			endif ()
			add_dependencies (crate_hosting_check CrateGainFixture)

			get_target_property (_crate_fixture_bundle CrateGainFixture SMTG_PLUGIN_PACKAGE_PATH)
			enable_testing ()
			if (_crate_fixture_bundle)
				add_test (NAME crate_hosting_check
				          COMMAND crate_hosting_check "${_crate_fixture_bundle}")
			endif ()
		endif ()
	else ()
		message (STATUS "VST3 SDK not found (CRATE_VST3_SDK_DIR='${CRATE_VST3_SDK_DIR}').")
		message (STATUS "Building crate_core and crate_tests only; the .vst3 bundle is skipped.")
	endif ()
endif ()
