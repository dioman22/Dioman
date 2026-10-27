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
			GIT_TAG v3.7.11_build_40
			GIT_SUBMODULES base pluginterfaces public.sdk cmake
			GIT_SUBMODULES_RECURSE TRUE
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

		smtg_add_vst3plugin (Crate
			source/vst3/crateentry.cpp
			source/vst3/cratecontroller.cpp
			source/vst3/crateprocessor.cpp
			source/vst3/hostcontext.cpp
			source/vst3/vst3instance.cpp
			source/vst3/vst3loader.cpp
		)
		target_include_directories (Crate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source)
		target_link_libraries (Crate PRIVATE crate_core)
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
	else ()
		message (STATUS "VST3 SDK not found (CRATE_VST3_SDK_DIR='${CRATE_VST3_SDK_DIR}').")
		message (STATUS "Building crate_core and crate_tests only; the .vst3 bundle is skipped.")
	endif ()
endif ()
